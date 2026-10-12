#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

#include "RtAudio.h"
#include "NAM/get_dsp.h"

using Sample = int16_t;

struct NAMTestState {
    unsigned int channels;
    std::unique_ptr<nam::DSP> model;
    std::vector<NAM_SAMPLE> input;
    std::vector<NAM_SAMPLE> output;
    std::atomic<unsigned long long> processCount;
    std::atomic<unsigned long long> processTotalUs;
    std::atomic<unsigned long long> processMaxUs;
};

void usage(void) {
    // Prints the NAM duplex test command format.
    std::cout << "\nusage: NAMTest N fs <iDevice> <oDevice> <iChannelOffset> <oChannelOffset>\n";
    std::cout << "    where N = number of channels and fs = sample rate.\n\n";
    exit(0);
}

int inoutNAM(void *outputBuffer,
             void *inputBuffer,
             unsigned int nBufferFrames,
             double,
             RtAudioStreamStatus,
             void *data) {
    // Converts the first input channel with NAM and writes it to every output channel.
    NAMTestState *state = (NAMTestState *)data;
    Sample *input = (Sample *)inputBuffer;
    Sample *output = (Sample *)outputBuffer;

    for (unsigned int frame = 0; frame < nBufferFrames; ++frame)
        state->input[frame] = (NAM_SAMPLE)input[frame * state->channels] / 32768.0f;

    NAM_SAMPLE *namInput[] = {state->input.data()};
    NAM_SAMPLE *namOutput[] = {state->output.data()};
    const auto processStart = std::chrono::steady_clock::now();
    state->model->process(namInput, namOutput, (int)nBufferFrames);
    const auto processUs = (unsigned long long)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - processStart).count();
    state->processCount.fetch_add(1);
    state->processTotalUs.fetch_add(processUs);
    unsigned long long previousMax = state->processMaxUs.load();
    while (previousMax < processUs &&
           !state->processMaxUs.compare_exchange_weak(previousMax, processUs)) {
    }

    for (unsigned int frame = 0; frame < nBufferFrames; ++frame) {
        Sample sample = (Sample)(std::clamp((float)state->output[frame], -1.0f, 1.0f) * 32767.0f);
        for (unsigned int channel = 0; channel < state->channels; ++channel)
            output[frame * state->channels + channel] = sample;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    // Opens a duplex stream that applies the bundled NAM model to guitar input.
    if (argc < 3 || argc > 7)
        usage();

    unsigned int channels = (unsigned int)atoi(argv[1]);
    unsigned int sampleRate = (unsigned int)atoi(argv[2]);
    unsigned int inputDevice = argc > 3 ? (unsigned int)atoi(argv[3]) : 0;
    unsigned int outputDevice = argc > 4 ? (unsigned int)atoi(argv[4]) : 0;
    unsigned int inputOffset = argc > 5 ? (unsigned int)atoi(argv[5]) : 0;
    unsigned int outputOffset = argc > 6 ? (unsigned int)atoi(argv[6]) : 0;

    RtAudio audio;
    std::vector<unsigned int> deviceIds = audio.getDeviceIds();
    unsigned int bufferFrames = 128;
    RtAudio::StreamParameters inputParams;
    inputParams.deviceId = inputDevice == 0 ? audio.getDefaultInputDevice() : deviceIds[inputDevice];
    inputParams.nChannels = channels;
    inputParams.firstChannel = inputOffset;
    RtAudio::StreamParameters outputParams;
    outputParams.deviceId = outputDevice == 0 ? audio.getDefaultOutputDevice() : deviceIds[outputDevice];
    outputParams.nChannels = channels;
    outputParams.firstChannel = outputOffset;

    NAMTestState state = {};
    state.channels = channels;
    state.model = nam::get_dsp(
        std::filesystem::path("assets/NAM/VX TB30 BR Edge0 BAL2 CAB FREE.nam"));

    if (audio.openStream(&outputParams,
                         &inputParams,
                         RTAUDIO_SINT16,
                         sampleRate,
                         &bufferFrames,
                         &inoutNAM,
                         &state))
        return 1;

    state.model->Reset(sampleRate, (int)bufferFrames);
    state.input.assign(bufferFrames, 0.0f);
    state.output.assign(bufferFrames, 0.0f);
    std::cout << "Stream latency = " << audio.getStreamLatency() << " frames ("
              << (double)audio.getStreamLatency() * 1000.0 / sampleRate << " ms)\n";
    std::cout << "Running NAM duplex test. Press <enter> to quit.\n";
    audio.startStream();
    std::cin.get();
    audio.stopStream();
    audio.closeStream();

    unsigned long long processCount = state.processCount.load();
    double averageUs = processCount == 0 ? 0.0 :
        (double)state.processTotalUs.load() / processCount;
    std::cout << "NAM process: avg " << averageUs / 1000.0 << " ms, max "
              << (double)state.processMaxUs.load() / 1000.0 << " ms, block budget "
              << (double)bufferFrames * 1000.0 / sampleRate << " ms\n";
    return 0;
}
