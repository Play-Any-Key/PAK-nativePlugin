#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "RtAudio.h"

extern "C" {
#include <aubio/aubio.h>
}

typedef int16_t MY_TYPE;
#define FORMAT RTAUDIO_SINT16

struct PitchState {
    unsigned int channels;
    unsigned int bufferBytes;
    fvec_t *input;
    fvec_t *pitch;
    aubio_pitch_t *pitchDetector;
    double nextPrintTime;
};

std::string midiToNoteName(int midi) {
    // Converts a MIDI note number to note name with octave.
    const char *noteNames[] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B",
    };
    int octave = midi / 12 - 1;
    return std::string(noteNames[midi % 12]) + std::to_string(octave);
}

void usage(void) {
    // Command-line usage for the duplex pitch test.
    std::cout << "\nuseage: duplexPitch N fs <iDevice> <oDevice> <iChannelOffset> <oChannelOffset>\n";
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

int inoutPitch(void *outputBuffer,
               void *inputBuffer,
               unsigned int nBufferFrames,
               double streamTime,
               RtAudioStreamStatus status,
               void *data) {
    // Copies live input to output and analyzes the first input channel for pitch.
    if (status)
        std::cout << "Stream over/underflow detected." << std::endl;

    PitchState *state = (PitchState *)data;
    memcpy(outputBuffer, inputBuffer, state->bufferBytes);

    MY_TYPE *samples = (MY_TYPE *)inputBuffer;
    for (unsigned int i = 0; i < nBufferFrames; i++) {
        smpl_t sample = (smpl_t)samples[i * state->channels] / 32768.0f;
        fvec_set_sample(state->input, sample, i);
    }

    aubio_pitch_do(state->pitchDetector, state->input, state->pitch);

    if (streamTime >= state->nextPrintTime) {
        smpl_t pitchMidi = fvec_get_sample(state->pitch, 0);
        int midi = (int)std::round(pitchMidi);
        if (midi >= 0)
            std::cout << "\rnote = " << midiToNoteName(midi) << " (MIDI " << midi << ")   " << std::flush;
        else
            std::cout << "\rnote = --                 " << std::flush;
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
    PitchState state = {};
    state.channels = channels;
    state.bufferBytes = bufferFrames * channels * sizeof(MY_TYPE);
    state.nextPrintTime = 0.1;

    if (adac.openStream(&oParams,
                        &iParams,
                        FORMAT,
                        fs,
                        &bufferFrames,
                        &inoutPitch,
                        (void *)&state,
                        &options)) {
        goto cleanup;
    }

    if (adac.isStreamOpen() == false)
        goto cleanup;

    state.bufferBytes = bufferFrames * channels * sizeof(MY_TYPE);
    state.input = new_fvec(bufferFrames);
    state.pitch = new_fvec(1);
    state.pitchDetector = new_aubio_pitch("default", 2048, bufferFrames, fs);
    aubio_pitch_set_unit(state.pitchDetector, "midi");

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
    if (state.pitchDetector)
        del_aubio_pitch(state.pitchDetector);
    if (state.pitch)
        del_fvec(state.pitch);
    if (state.input)
        del_fvec(state.input);
    aubio_cleanup();

    return 0;
}
