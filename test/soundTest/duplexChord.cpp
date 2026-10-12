#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "RtAudio.h"

extern "C" {
#include <aubio/aubio.h>
}

typedef int16_t MY_TYPE;
#define FORMAT RTAUDIO_SINT16

struct OpenString {
    const char *name;
    smpl_t frequency;
};

struct ChordState {
    unsigned int channels;
    unsigned int sampleRate;
    unsigned int bufferBytes;
    fvec_t *input;
    cvec_t *spectrum;
    aubio_fft_t *fft;
    double nextPrintTime;
};

const OpenString OPEN_STRINGS[] = {
    {"E2", 82.41f},
    {"A2", 110.00f},
    {"D3", 146.83f},
    {"G3", 196.00f},
    {"B3", 246.94f},
    {"E4", 329.63f},
};

void usage(void) {
    // Command-line usage for the duplex chord test.
    std::cout << "\nuseage: duplexChord N fs <iDevice> <oDevice> <iChannelOffset> <oChannelOffset>\n";
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

int inoutChord(void *outputBuffer,
               void *inputBuffer,
               unsigned int nBufferFrames,
               double streamTime,
               RtAudioStreamStatus status,
               void *data) {
    // Copies live input to output and analyzes open-string frequency energy.
    if (status)
        std::cout << "Stream over/underflow detected." << std::endl;

    ChordState *state = (ChordState *)data;
    memcpy(outputBuffer, inputBuffer, state->bufferBytes);

    MY_TYPE *samples = (MY_TYPE *)inputBuffer;
    for (unsigned int i = 0; i < nBufferFrames; i++) {
        smpl_t sample = (smpl_t)samples[i * state->channels] / 32768.0f;
        fvec_set_sample(state->input, sample, i);
    }

    aubio_fft_do(state->fft, state->input, state->spectrum);

    if (streamTime >= state->nextPrintTime) {
        smpl_t totalEnergy = 0.0f;
        for (uint_t bin = 1; bin < state->spectrum->length; bin++) {
            smpl_t magnitude = cvec_norm_get_sample(state->spectrum, bin);
            totalEnergy += magnitude * magnitude;
        }

        std::cout << "\r";
        for (const OpenString &openString : OPEN_STRINGS) {
            smpl_t stringEnergy = 0.0f;
            for (int harmonic = 1; harmonic <= 4; harmonic++) {
                smpl_t frequency = openString.frequency * harmonic;
                uint_t centerBin = (uint_t)std::round(frequency * nBufferFrames / state->sampleRate);
                uint_t startBin = centerBin > 1 ? centerBin - 1 : 1;
                uint_t endBin = std::min(centerBin + 1, state->spectrum->length - 1);

                for (uint_t bin = startBin; bin <= endBin; bin++) {
                    smpl_t magnitude = cvec_norm_get_sample(state->spectrum, bin);
                    stringEnergy += (magnitude * magnitude) / harmonic;
                }
            }

            smpl_t percent = totalEnergy > 0.0f ? stringEnergy / totalEnergy * 100.0f : 0.0f;
            std::cout << openString.name << "=" << std::fixed << std::setprecision(2) << percent << "% ";
        }
        std::cout << "   " << std::flush;
        state->nextPrintTime += 0.1;
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

    unsigned int bufferFrames = 4096;
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
    ChordState state = {};
    state.channels = channels;
    state.sampleRate = fs;
    state.bufferBytes = bufferFrames * channels * sizeof(MY_TYPE);
    state.nextPrintTime = 0.1;

    if (adac.openStream(&oParams,
                        &iParams,
                        FORMAT,
                        fs,
                        &bufferFrames,
                        &inoutChord,
                        (void *)&state,
                        &options)) {
        goto cleanup;
    }

    if (adac.isStreamOpen() == false)
        goto cleanup;

    state.bufferBytes = bufferFrames * channels * sizeof(MY_TYPE);
    state.input = new_fvec(bufferFrames);
    state.spectrum = new_cvec(bufferFrames);
    state.fft = new_aubio_fft(bufferFrames);

    std::cout << "\nStream latency = " << adac.getStreamLatency() << " frames" << std::endl;

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
    if (state.fft)
        del_aubio_fft(state.fft);
    if (state.spectrum)
        del_cvec(state.spectrum);
    if (state.input)
        del_fvec(state.input);
    aubio_cleanup();

    return 0;
}
