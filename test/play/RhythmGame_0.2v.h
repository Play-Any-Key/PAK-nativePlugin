#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "../feature/ChartParser.h"
#include "RtAudio.h"

extern "C" {
#include <aubio/aubio.h>
}

typedef int16_t MY_TYPE;
#define FORMAT RTAUDIO_SINT16

const double PERFECT_MS = 60.0;
const double GOOD_MS = 140.0;
const double BAD_MS = 240.0;
const double COUNTDOWN_SECONDS = 5.0;
const int PITCH_TOLERANCE = 0;
const double PITCH_SETTLE_MS = 80.0;

struct AudioBlock {
    double streamTime;
    unsigned int frames;
    uintptr_t sourceSamplesAddress;
    uintptr_t copiedSamplesAddress;
    MY_TYPE firstSampleAtPush;
    std::vector<MY_TYPE> samples;
};

struct AudioSpscQueue {
    std::vector<AudioBlock> blocks;
    std::atomic<unsigned int> readIndex;
    std::atomic<unsigned int> writeIndex;
};

struct PitchObservation {
    double timeMs;
    float rawPitch;
    int midi;
};

struct PendingJudgment {
    int noteIndex;
    double onsetMs;
    double blockMs;
    double errorMs;
    double deadlineMs;
};

struct RhythmState {
    ChartParser::Chart chart;
    unsigned int channels;
    unsigned int bufferBytes;
    AudioSpscQueue audioQueue;
    fvec_t *input;
    fvec_t *pitch;
    fvec_t *onset;
    aubio_pitch_t *pitchDetector;
    aubio_onset_t *onsetDetector;
    float lastDetectedPitch;
    std::string lastResult;
    std::vector<std::string> noteResults;
    std::vector<std::string> pitchDiagnostics;
    std::vector<PitchObservation> pitchObservations;
    std::vector<PendingJudgment> pendingJudgments;
    double nextPrintTime;
    int nextNoteIndex;
    int lastDetectedMidi;
    bool gameStarted;
    bool summaryPrinted;
    float inputGain;
    float outputGain;
    float lpfAlpha;
    std::vector<float> lpfState;
    std::atomic<double> latestCallbackStreamTime;
    std::atomic<unsigned int> pushedAudioBlocks;
    std::atomic<unsigned int> droppedAudioBlocks;
    unsigned int judgedAudioBlocks;
    unsigned int pointerAliasBlocks;
    unsigned int changedFirstSampleBlocks;
    unsigned int onsetEvents;
    unsigned int acceptedOnsetEvents;
    unsigned int outsideWindowOnsetEvents;
    unsigned int zeroMidiOnsetEvents;
    unsigned int wrongMidiOnsetEvents;
    double queueLagMinMs;
    double queueLagMaxMs;
    double queueLagSumMs;
    std::atomic<bool> quitRequested;
};

void prepareAudioQueue(AudioSpscQueue *queue, unsigned int blockCount, unsigned int sampleCount);
bool pushAudioBlock(AudioSpscQueue *queue,
                    MY_TYPE *samples,
                    unsigned int frames,
                    unsigned int sampleCount,
                    double streamTime);
AudioBlock *frontAudioBlock(AudioSpscQueue *queue);
void popAudioBlock(AudioSpscQueue *queue);
std::string formatSeconds(double seconds);
std::string makeProgressBar(double streamTime, int durationMs);
std::string shortenText(const std::string &text, int maxLength);
std::string makeTimingCue(double remainSeconds);
const char *judgeTiming(double absErrorMs);
float lpf(float input, float previous, float alpha);
void printHud(RhythmState *state, double streamTime);
void printCountdown(double gameTime);
void printSummary(RhythmState *state);
void usage(void);
unsigned int getDeviceIndex(std::vector<std::string> deviceNames, bool isInput = false);
void processMonitorDsp(RhythmState *state,
                       MY_TYPE *output,
                       MY_TYPE *input,
                       unsigned int nBufferFrames);
void processJudgmentBlock(RhythmState *state, AudioBlock *block);
void handleCli(RhythmState *state);
void judgeThreadMain(RhythmState *state);
int inoutRhythmGame(void *outputBuffer,
                    void *inputBuffer,
                    unsigned int nBufferFrames,
                    double streamTime,
                    RtAudioStreamStatus status,
                    void *data);
void printConnectedDevices(RtAudio &adac, unsigned int inputDeviceId, unsigned int outputDeviceId);
