#include "input.h"

#include "eventQueue.h"
#include "plugin_state.h"

#include <cmath>

void pushGuitarInputEvent(PluginState *state, const GuitarInputEvent &event) {
    // Pushes one detected guitar input for Unity to poll later.
    PluginEvent output = {};
    output.type = PluginEvent_GuitarInput;
    output.guitarInput = event;
    pushPluginEvent(&state->eventQueue, output);
}

int pollGuitarInputEvent(PluginState *state, GuitarInputEvent *outEvent) {
    // Pops one pending guitar input event for the Unity-side polling API.
    PluginEvent event = {};
    if (!pollPluginEvent(&state->eventQueue, PluginEvent_GuitarInput, &event))
        return 0;

    *outEvent = event.guitarInput;
    return 1;
}

bool selectGuitarPitchObservation(PluginState *state,
                                  const PendingGuitarInput &pending,
                                  PitchObservation *selected) {
    // Selects the non-zero pitch closest to the guitar input settle deadline.
    bool found = false;
    double bestTimeDistance = 1000000.0;

    for (const PitchObservation &observation : state->pitchObservations) {
        if (observation.audioTimeMs < pending.onsetAudioTimeMs)
            continue;
        if (observation.audioTimeMs > pending.deadlineAudioTimeMs)
            continue;
        if (observation.midi == 0)
            continue;

        double timeDistance = std::abs(observation.audioTimeMs - pending.deadlineAudioTimeMs);
        if (!found || timeDistance < bestTimeDistance) {
            *selected = observation;
            bestTimeDistance = timeDistance;
            found = true;
        }
    }

    return found;
}

void finalizePendingGuitarInputs(PluginState *state, double audioTimeMs) {
    // Emits guitar input events after their pitch settle window has closed.
    size_t index = 0;
    while (index < state->pendingGuitarInputs.size()) {
        PendingGuitarInput pending = state->pendingGuitarInputs[index];
        if (audioTimeMs < pending.deadlineAudioTimeMs) {
            index++;
            continue;
        }

        PitchObservation selected = {};
        if (selectGuitarPitchObservation(state, pending, &selected)) {
            double lastEventTime = state->lastGuitarInputEventAudioTimeMs.load();
            if (pending.onsetAudioTimeMs - lastEventTime >= state->guitarInputIntervalMs.load()) {
                GuitarInputEvent event = {};
                event.midi = selected.midi;
                event.audioTimeMs = pending.onsetAudioTimeMs;
                pushGuitarInputEvent(state, event);
                state->lastGuitarInputEventAudioTimeMs.store(pending.onsetAudioTimeMs);
            }
        }

        state->pendingGuitarInputs.erase(state->pendingGuitarInputs.begin() + index);
    }
}

void processGuitarInputBlock(PluginState *state,
                             bool hasOnset,
                             double onsetAudioTimeMs,
                             double audioTimeMs) {
    // Handles one analyzed audio block as guitar-control input.
    if (hasOnset) {
        state->pendingGuitarInputs.push_back({onsetAudioTimeMs,
                                              onsetAudioTimeMs + PITCH_SETTLE_MS});
    }
    finalizePendingGuitarInputs(state, audioTimeMs);
}
