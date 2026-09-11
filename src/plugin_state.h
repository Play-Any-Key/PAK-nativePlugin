#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "ChartParser.h"
#include "audioQueue.h"
#include "eventQueue.h"
#include "input.h"
#include "judge.h"
#include "main.h"
#include "NAM/get_dsp.h"


extern "C" {
#include <aubio/aubio.h>
}

constexpr double COUNTDOWN_SECONDS = 3.0;
constexpr double COUNTDOWN_MS = COUNTDOWN_SECONDS * 1000.0;
constexpr double PITCH_SETTLE_MS = 80.0;
constexpr double CHORD_SETTLE_MS = 160.0;
constexpr unsigned int CHORD_FFT_SIZE = 16384;
constexpr unsigned int FINGERING_PCM_RING_FRAMES = CHORD_FFT_SIZE;

enum SessionMode {
    SessionMode_None = 0,
    SessionMode_GuitarInput = 1,
    SessionMode_Judge = 2,
    SessionMode_SlowPractice = 3,
    SessionMode_FingeringPractice = 4,
    SessionMode_AudioTest = 5,
};

struct PitchObservation {
    double audioTimeMs;
    double chartTimeMs;
    int midi;
};

struct PluginState {
    ChartParser::Chart chart;
    unsigned int channels;
    unsigned int sampleRate;
    unsigned int bufferFrames;
    AudioSpscQueue audioQueue;
    PluginEventQueue eventQueue;
    PluginEventQueue fingeringTestRawOnsetQueue;
    fvec_t *input;
    fvec_t *pitch;
    fvec_t *onset;
    fvec_t *chordInput;
    cvec_t *chordSpectrum;
    aubio_pitch_t *pitchDetector;
    aubio_onset_t *onsetDetector;
    aubio_fft_t *chordFft;
    std::vector<float> lpfState;
    std::string namModelPath = "assets/NAM/VX TB30 BR Edge0 BAL2 CAB FREE.nam";
    std::unique_ptr<nam::DSP> namModel;
    std::vector<NAM_SAMPLE> namInput;
    std::vector<NAM_SAMPLE> namOutput;
    std::vector<MY_TYPE> songSamples;
    unsigned int songChannels;
    unsigned long long songFrames;
    std::vector<MY_TYPE> metronomeSamples;
    unsigned int metronomeChannels;
    unsigned long long metronomeFrames;
    std::atomic<float> inputGain;
    std::atomic<float> outputGain;
    std::atomic<float> songVolume;
    std::atomic<float> lpfAlpha;
    bool audioTestMode;
    std::atomic<float> audioTestOutputLevelDb;
    std::atomic<bool> stopRequested;
    std::atomic<bool> gameStarted;
    std::atomic<bool> summaryFinished;
    std::atomic<int> requestedSessionMode;
    std::atomic<int> sessionMode;
    std::atomic<unsigned int> queuedAudioBlocks;
    std::atomic<unsigned int> processedAudioBlocks;
    std::atomic<unsigned int> droppedAudioBlocks;
    std::atomic<unsigned int> droppedJudgeEvents;
    std::atomic<unsigned int> detectedOnsets;
    std::atomic<unsigned int> startedFingeringJudgments;
    std::atomic<unsigned int> passedChordJudgments;
    std::atomic<unsigned int> failedChordJudgments;
    std::atomic<double> guitarInputIntervalMs;
    std::atomic<double> lastGuitarInputEventAudioTimeMs;
    std::atomic<double> lastStreamTime;
    std::atomic<double> lastChartTimeMs;
    std::atomic<float> practiceSpeed;
    std::atomic<double> sessionStreamTimeOffset;
    std::atomic<bool> sessionClockStarted;
    std::atomic<bool> isPaused;
    std::atomic<bool> resumeRequested;
    std::atomic<double> lastRawStreamTime;
    std::atomic<double> pauseStartedStreamTime;
    std::atomic<double> pausedStreamDuration;
    std::atomic<int> nextNoteIndex;
    std::atomic<bool> ignoreOnsetsDuringJudgmentCollection;
    bool advanceFingeringTargetOnFailure;
    std::vector<PitchObservation> pitchObservations;
    std::vector<PendingGuitarInput> pendingGuitarInputs;
    std::vector<PendingJudgment> pendingJudgments;
    std::vector<float> fingeringPcmRing;
    int lastDetectedMidi;
};
