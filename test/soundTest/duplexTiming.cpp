#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "RtAudio.h"

extern "C" {
#include <aubio/aubio.h>
}

typedef int16_t MY_TYPE;
#define FORMAT RTAUDIO_SINT16

const double BEAT_INTERVAL = 0.8;
const int BEATS_PER_CYCLE = 4;
const double FIRST_TARGET_TIME = BEAT_INTERVAL * 3.0;
const double CYCLE_INTERVAL = BEAT_INTERVAL * BEATS_PER_CYCLE;
const double PERFECT_MS = 30.0;
const double GOOD_MS = 70.0;
const double BAD_MS = 120.0;

struct TimingState {
    unsigned int channels;
    unsigned int bufferBytes;
    fvec_t *input;
    fvec_t *onset;
    aubio_onset_t *onsetDetector;
    std::string lastResult;
    int printedCount;
    int lastHitTargetIndex;
    int lastMissTargetIndex;
};

void usage(void) {
    // Command-line usage for the duplex timing test.
    std::cout << "\nuseage: duplexTiming N fs <iDevice> <oDevice> <iChannelOffset> <oChannelOffset>\n";
    std::cout << "    where N = number of channels,\n";
    std::cout << "    fs = the sample rate,\n";
    std::cout << "    iDevice = optional input device index to use (default = 0),\n";
    std::cout << "    oDevice = optional output device index to use (default = 0),\n";
    std::cout << "    iChannelOffset = an optional input channel offset (default = 0),\n";
    std::cout << "    and oChannelOffset = optional output channel offset (default = 0).\n\n";
    exit(0);
}

unsigned int getDeviceIndex(std::vector<std::string> deviceNames, bool isInput = false) {
    // Lets the user select an audio device when the requested index is invalid.
    unsigned int i;
    std::string keyHit;
    std::cout << '\n';
    for (i = 0; i < deviceNames.size(); i++)
        std::cout << "  Device #" << i << ": " << deviceNames[i] << '\n';
    do {
        if (isInput)
            std::cout << "\nChoose an input device #: ";
        else
            std::cout << "\nChoose an output device #: ";
        std::cin >> i;
    } while (i >= deviceNames.size());
    std::getline(std::cin, keyHit);
    return i;
}

int inoutTiming(void *outputBuffer,
                void *inputBuffer,
                unsigned int nBufferFrames,
                double streamTime,
                RtAudioStreamStatus status,
                void *data) {
    // Copies live input to output and judges detected onset timing against beat 4.
    if (status)
        std::cout << "Stream over/underflow detected." << std::endl;

    TimingState *state = (TimingState *)data;
    memcpy(outputBuffer, inputBuffer, state->bufferBytes);

    int count = ((int)std::floor(streamTime / BEAT_INTERVAL) % BEATS_PER_CYCLE) + 1;
    if (count != state->printedCount) {
        std::cout << "\rCount: " << count << " | Last: " << state->lastResult << "                    "
                  << std::flush;
        state->printedCount = count;
    }

    MY_TYPE *samples = (MY_TYPE *)inputBuffer;
    for (unsigned int i = 0; i < nBufferFrames; i++) {
        smpl_t sample = (smpl_t)samples[i * state->channels] / 32768.0f;
        fvec_set_sample(state->input, sample, i);
    }

    aubio_onset_do(state->onsetDetector, state->input, state->onset);
    if (fvec_get_sample(state->onset, 0) != 0.0f) {
        double onsetTime = aubio_onset_get_last_s(state->onsetDetector);
        int targetIndex = (int)std::round((onsetTime - FIRST_TARGET_TIME) / CYCLE_INTERVAL);
        if (targetIndex < 0)
            targetIndex = 0;

        double targetTime = FIRST_TARGET_TIME + CYCLE_INTERVAL * targetIndex;
        double errorMs = (onsetTime - targetTime) * 1000.0;
        double absErrorMs = std::abs(errorMs);
        const char *result = "Miss";

        if (absErrorMs <= PERFECT_MS)
            result = "Perfect";
        else if (absErrorMs <= GOOD_MS)
            result = "Good";
        else if (absErrorMs <= BAD_MS)
            result = "Bad";

        if (absErrorMs <= BAD_MS)
            state->lastHitTargetIndex = targetIndex;

        std::ostringstream resultText;
        resultText << result << " (" << std::showpos << std::fixed << std::setprecision(1) << errorMs
                   << std::noshowpos << " ms)";
        state->lastResult = resultText.str();
        std::cout << "\rCount: " << count << " | Last: " << state->lastResult << "                    "
                  << std::flush;
    }

    if (streamTime >= FIRST_TARGET_TIME + BAD_MS / 1000.0) {
        int missedTargetIndex = (int)std::floor((streamTime - FIRST_TARGET_TIME - BAD_MS / 1000.0) / CYCLE_INTERVAL);
        if (missedTargetIndex > state->lastMissTargetIndex) {
            if (state->lastHitTargetIndex < missedTargetIndex) {
                state->lastResult = "Miss (no input)";
                std::cout << "\rCount: " << count << " | Last: " << state->lastResult << "                    "
                          << std::flush;
            }
            state->lastMissTargetIndex = missedTargetIndex;
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {
    unsigned int channels, fs, oDevice = 0, iDevice = 0, iOffset = 0, oOffset = 0;

    // Minimal command-line checking.
    if (argc < 3 || argc > 7)
        usage();

    RtAudio adac;
    std::vector<unsigned int> deviceIds = adac.getDeviceIds();
    if (deviceIds.size() < 1) {
        std::cout << "\nNo audio devices found!\n";
        exit(1);
    }

    channels = (unsigned int)atoi(argv[1]);
    fs = (unsigned int)atoi(argv[2]);
    if (argc > 3)
        iDevice = (unsigned int)atoi(argv[3]);
    if (argc > 4)
        oDevice = (unsigned int)atoi(argv[4]);
    if (argc > 5)
        iOffset = (unsigned int)atoi(argv[5]);
    if (argc > 6)
        oOffset = (unsigned int)atoi(argv[6]);

    adac.showWarnings(true);

    unsigned int bufferFrames = 512;
    RtAudio::StreamParameters iParams, oParams;
    iParams.nChannels = channels;
    iParams.firstChannel = iOffset;
    oParams.nChannels = channels;
    oParams.firstChannel = oOffset;

    if (iDevice == 0)
        iParams.deviceId = adac.getDefaultInputDevice();
    else {
        if (iDevice >= deviceIds.size())
            iDevice = getDeviceIndex(adac.getDeviceNames(), true);
        iParams.deviceId = deviceIds[iDevice];
    }
    if (oDevice == 0)
        oParams.deviceId = adac.getDefaultOutputDevice();
    else {
        if (oDevice >= deviceIds.size())
            oDevice = getDeviceIndex(adac.getDeviceNames());
        oParams.deviceId = deviceIds[oDevice];
    }

    RtAudio::StreamOptions options;
    TimingState state = {};
    state.channels = channels;
    state.bufferBytes = bufferFrames * channels * sizeof(MY_TYPE);
    state.lastResult = "Waiting";
    state.printedCount = 0;
    state.lastHitTargetIndex = -1;
    state.lastMissTargetIndex = -1;

    if (adac.openStream(&oParams,
                        &iParams,
                        FORMAT,
                        fs,
                        &bufferFrames,
                        &inoutTiming,
                        (void *)&state,
                        &options)) {
        goto cleanup;
    }

    if (adac.isStreamOpen() == false)
        goto cleanup;

    state.bufferBytes = bufferFrames * channels * sizeof(MY_TYPE);
    state.input = new_fvec(bufferFrames);
    state.onset = new_fvec(1);
    state.onsetDetector = new_aubio_onset("default", 1024, bufferFrames, fs);
    aubio_onset_set_threshold(state.onsetDetector, 0.3f);

    std::cout << "\nStream latency = " << adac.getStreamLatency() << " frames" << std::endl;
    std::cout << "75 BPM timing: play on count 4.\n";

    if (adac.startStream())
        goto cleanup;

    char input;
    std::cout << "\nRunning ... press <enter> to quit (buffer frames = " << bufferFrames << ").\n";
    std::cin.get(input);

cleanup:
    std::cout << std::endl;
    if (adac.isStreamRunning())
        adac.stopStream();
    if (adac.isStreamOpen())
        adac.closeStream();
    if (state.onsetDetector)
        del_aubio_onset(state.onsetDetector);
    if (state.onset)
        del_fvec(state.onset);
    if (state.input)
        del_fvec(state.input);
    aubio_cleanup();

    return 0;
}
