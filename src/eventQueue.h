#pragma once

#include "main.h"

#include <mutex>
#include <vector>

enum PluginEventType {
    PluginEvent_None = 0,
    PluginEvent_Judge = 1,
    PluginEvent_GuitarInput = 2,
    PluginEvent_FingeringTestRawOnset = 3,
};

struct PluginEvent {
    int type;
    JudgeEvent judge;
    GuitarInputEvent guitarInput;
    FingeringTestRawOnset fingeringTestRawOnset;
};

struct PluginEventQueue {
    std::vector<PluginEvent> events;
    unsigned int readIndex;
    unsigned int writeIndex;
    std::mutex mutex;
};

void preparePluginEventQueue(PluginEventQueue *queue, unsigned int eventCount);
bool pushPluginEvent(PluginEventQueue *queue, const PluginEvent &event);
int pollPluginEvent(PluginEventQueue *queue, int type, PluginEvent *outEvent);
