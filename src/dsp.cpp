#include "dsp.h"

#include <algorithm>
#include <cmath>

float lpf(float input, float previous, float alpha) {
    // Applies a simple one-pole low-pass filter.
    return previous + alpha * (input - previous);
}

void processMonitorDsp(PluginState *state,
                       MY_TYPE *output,
                       MY_TYPE *input,
                       unsigned int nBufferFrames,
                       double sessionStreamTime,
                       double chartTimeMs,
                       double chartTimeScale) {
    // Applies NAM to guitar input, then mixes chart audio and applies monitor DSP.
    for (unsigned int frame = 0; frame < nBufferFrames; frame++)
        state->namInput[frame] =
            (NAM_SAMPLE)input[frame * state->channels] / 32768.0 * state->inputGain.load();

    NAM_SAMPLE *namInputChannels[] = {state->namInput.data()};
    NAM_SAMPLE *namOutputChannels[] = {state->namOutput.data()};
    state->namModel->process(namInputChannels, namOutputChannels, (int)nBufferFrames);

    for (unsigned int frame = 0; frame < nBufferFrames; frame++) {
        for (unsigned int channel = 0; channel < state->channels; channel++) {
            unsigned int index = frame * state->channels + channel;
            float sample = (float)state->namOutput[frame];

            double songTimeSeconds = sessionStreamTime + (double)frame / state->sampleRate
                                     - COUNTDOWN_SECONDS;
            int sessionMode = state->sessionMode.load();
            if (sessionMode != SessionMode_SlowPractice &&
                sessionMode != SessionMode_FingeringPractice && songTimeSeconds >= 0.0) {
                unsigned long long songFrame =
                    (unsigned long long)(songTimeSeconds * state->sampleRate);
                if (songFrame < state->songFrames) {
                    unsigned int songChannel = state->songChannels == 1 ? 0 : channel % state->songChannels;
                    unsigned long long songIndex = songFrame * state->songChannels + songChannel;
                    sample += (float)state->songSamples[songIndex] / 32768.0f *
                              state->songVolume.load();
                }
            }

            if (sessionMode == SessionMode_SlowPractice) {
                double beatMs = 60000.0 / state->chart.bpm;
                double frameChartTimeMs = chartTimeMs +
                                          (double)frame * 1000.0 / state->sampleRate * chartTimeScale;
                double beatPosition = (frameChartTimeMs - state->chart.audioOffsetMs) / beatMs;
                double beatStartMs = state->chart.audioOffsetMs + std::floor(beatPosition) * beatMs;
                unsigned long long metronomeFrame = (unsigned long long)(
                    (frameChartTimeMs - beatStartMs) * state->sampleRate /
                    (1000.0 * chartTimeScale));
                if (metronomeFrame < state->metronomeFrames) {
                    unsigned int metronomeChannel = state->metronomeChannels == 1
                                                        ? 0
                                                        : channel % state->metronomeChannels;
                    unsigned long long metronomeIndex = metronomeFrame * state->metronomeChannels +
                                                       metronomeChannel;
                    sample += (float)state->metronomeSamples[metronomeIndex] / 32768.0f *
                              state->songVolume.load();
                }
            }

            sample = lpf(sample, state->lpfState[channel], state->lpfAlpha.load());
            state->lpfState[channel] = sample;
            sample *= state->outputGain.load();

            sample = std::clamp(sample, -1.0f, 1.0f);
            output[index] = (MY_TYPE)(sample * 32767.0f);
        }
    }
}
