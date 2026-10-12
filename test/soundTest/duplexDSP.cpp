#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "RtAudio.h"

typedef int16_t MY_TYPE;
#define FORMAT RTAUDIO_SINT16

struct DSPState {
    unsigned int channels;
    float inputGain;
    float outputGain;
    float lpfAlpha;
    std::vector<float> lpfState;
};

void usage(void) {
    // Command-line usage for the duplex DSP test.
    std::cout << "\nuseage: duplexDSP N fs <iDevice> <oDevice> <iChannelOffset> <oChannelOffset>\n";
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

float lpf(float input, float previous, float alpha) {
    // Applies a simple one-pole low-pass filter.
    return previous + alpha * (input - previous);
}

int inoutDSP(void *outputBuffer,
             void *inputBuffer,
             unsigned int nBufferFrames,
             double streamTime,
             RtAudioStreamStatus status,
             void *data) {
    // Processes PCM input as input gain > tanh overdrive > LPF > output gain.
    if (status)
        std::cout << "Stream over/underflow detected." << std::endl;

    DSPState *state = (DSPState *)data;
    MY_TYPE *input = (MY_TYPE *)inputBuffer;
    MY_TYPE *output = (MY_TYPE *)outputBuffer;

    for (unsigned int frame = 0; frame < nBufferFrames; frame++) {
        for (unsigned int channel = 0; channel < state->channels; channel++) {
            unsigned int index = frame * state->channels + channel;
            float sample = (float)input[index] / 32768.0f;

            sample *= state->inputGain;
            sample = std::tanh(sample);
            sample = lpf(sample, state->lpfState[channel], state->lpfAlpha);
            state->lpfState[channel] = sample;
            sample *= state->outputGain;

            sample = std::clamp(sample, -1.0f, 1.0f);
            output[index] = (MY_TYPE)(sample * 32767.0f);
        }
    }
    return 0;
}

void printConnectedDevices(RtAudio &adac, unsigned int inputDeviceId, unsigned int outputDeviceId) {
    // Prints the selected input and output device names.
    RtAudio::DeviceInfo inputInfo = adac.getDeviceInfo(inputDeviceId);
    RtAudio::DeviceInfo outputInfo = adac.getDeviceInfo(outputDeviceId);

    std::cout << "\nInput Device  : " << inputInfo.name << std::endl;
    std::cout << "Output Device : " << outputInfo.name << std::endl;
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

    unsigned int bufferFrames = 128;
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
    printConnectedDevices(adac, iParams.deviceId, oParams.deviceId);

    RtAudio::StreamOptions options;
    DSPState state = {};
    state.channels = channels;
    state.inputGain = 4.0f;
    state.outputGain = 0.5f;
    state.lpfAlpha = 0.2f;
    state.lpfState.assign(channels, 0.0f);

    if (adac.openStream(
            &oParams, &iParams, FORMAT, fs, &bufferFrames, &inoutDSP, (void *)&state, &options)) {
        goto cleanup;
    }

    if (adac.isStreamOpen() == false)
        goto cleanup;

    std::cout << "\nStream latency = " << adac.getStreamLatency() << " frames" << std::endl;

    if (adac.startStream())
        goto cleanup;

    char input;
    std::cout << "\nRunning ... press <enter> to quit (buffer frames = " << bufferFrames << ").\n";
    std::cin.get(input);

cleanup:
    if (adac.isStreamRunning())
        adac.stopStream();
    if (adac.isStreamOpen())
        adac.closeStream();

    return 0;
}
