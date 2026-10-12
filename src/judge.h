#pragma once

#include "audioQueue.h"
#include "main.h"

#include <vector>

struct PluginState;

struct PendingJudgment {
    int noteIndex;
    double onsetChartTimeMs;
    double onsetAudioTimeMs;
    double errorMs;
    double deadlineChartTimeMs;
    double deadlineAudioTimeMs;
    bool isFingeringPractice;
    unsigned long long onsetFrame;
    unsigned long long endFrame;
    std::vector<float> chordSamples;
};

int pollJudgeEvent(PluginState *state, JudgeEvent *outEvent);
int pollFingeringTestRawOnset(PluginState *state, FingeringTestRawOnset *outOnset);
void processJudgmentBlock(PluginState *state, AudioBlock *block);
void judgeThreadMain(PluginState *state);
