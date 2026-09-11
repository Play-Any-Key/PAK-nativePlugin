#include "main.h"

#include <cstring>
#include <thread>

#include "RtAudio.h"
#include "audioQueue.h"
#include "dsp.h"
#include "eventQueue.h"
#include "input.h"
#include "judge.h"
#include "song.h"
#include "setting.h"

RtAudio *g_adac = nullptr;
PluginState g_state = {};
static std::thread g_judgeThread;

extern "C" PLUGIN_API const char *GetPluginVersion(void) {
    // Returns the version of the loaded native plugin.
    return "0.5.2";
}

int inoutRhythmGame(void *outputBuffer,
                    void *inputBuffer,
                    unsigned int nBufferFrames,
                    double streamTime,
                    RtAudioStreamStatus,
                    void *data) {
    // Queues input for judgment, then writes processed monitor audio to output.
    PluginState *state = (PluginState *)data;
    MY_TYPE *input = (MY_TYPE *)inputBuffer;
    MY_TYPE *output = (MY_TYPE *)outputBuffer;
    unsigned int sampleCount = nBufferFrames * state->channels;

    if (state->sessionMode.load() == SessionMode_AudioTest) {
        std::memcpy(output, input, sampleCount * sizeof(MY_TYPE));
        if (pushAudioBlock(&state->audioQueue,
                           input,
                           nBufferFrames,
                           sampleCount,
                           streamTime,
                           0.0,
                           0.0))
            state->queuedAudioBlocks.fetch_add(1);
        else
            state->droppedAudioBlocks.fetch_add(1);
        return 0;
    }

    if (!state->sessionClockStarted.exchange(true))
        state->sessionStreamTimeOffset.store(streamTime);
    double rawSessionStreamTime = streamTime - state->sessionStreamTimeOffset.load();
    state->lastRawStreamTime.store(rawSessionStreamTime);

    if (state->isPaused.load()) {
        std::memset(output, 0, sampleCount * sizeof(MY_TYPE));
        return 0;
    }

    if (state->resumeRequested.exchange(false)) {
        double pausedDuration = rawSessionStreamTime - state->pauseStartedStreamTime.load();
        state->pausedStreamDuration.store(state->pausedStreamDuration.load() + pausedDuration);
    }
    double sessionStreamTime = rawSessionStreamTime - state->pausedStreamDuration.load();

    state->lastStreamTime.store(sessionStreamTime);
    double chartTimeMs = sessionStreamTime * 1000.0 - COUNTDOWN_MS;
    double chartTimeScale = 1.0;
    int sessionMode = state->sessionMode.load();
    if (sessionMode == SessionMode_SlowPractice) {
        chartTimeMs = state->lastChartTimeMs.load();
        chartTimeScale = state->practiceSpeed.load();
        state->lastChartTimeMs.store(chartTimeMs +
                                     nBufferFrames * 1000.0 / state->sampleRate * chartTimeScale);
    } else if (sessionMode == SessionMode_FingeringPractice) {
        int noteIndex = state->nextNoteIndex.load();
        chartTimeMs = noteIndex < (int)state->chart.notes.size()
                          ? state->chart.notes[noteIndex].startMs
                          : state->chart.durationMs;
        chartTimeScale = 0.0;
        state->lastChartTimeMs.store(chartTimeMs);
    } else {
        state->lastChartTimeMs.store(chartTimeMs);
    }

    if (pushAudioBlock(&state->audioQueue,
                       input,
                       nBufferFrames,
                       sampleCount,
                       sessionStreamTime,
                       chartTimeMs,
                       chartTimeScale))
        state->queuedAudioBlocks.fetch_add(1);
    else
        state->droppedAudioBlocks.fetch_add(1);
    processMonitorDsp(state,
                      output,
                      input,
                      nBufferFrames,
                      sessionStreamTime,
                      chartTimeMs,
                      chartTimeScale);
    return 0;
}

void releaseAubio(PluginState *state) {
    // Releases aubio objects owned by the plugin state.
    if (state->pitchDetector)
        del_aubio_pitch(state->pitchDetector);
    if (state->onsetDetector)
        del_aubio_onset(state->onsetDetector);
    if (state->chordFft)
        del_aubio_fft(state->chordFft);
    if (state->pitch)
        del_fvec(state->pitch);
    if (state->onset)
        del_fvec(state->onset);
    if (state->chordSpectrum)
        del_cvec(state->chordSpectrum);
    if (state->chordInput)
        del_fvec(state->chordInput);
    if (state->input)
        del_fvec(state->input);
    state->pitchDetector = nullptr;
    state->onsetDetector = nullptr;
    state->chordFft = nullptr;
    state->pitch = nullptr;
    state->onset = nullptr;
    state->chordSpectrum = nullptr;
    state->chordInput = nullptr;
    state->input = nullptr;
}

extern "C" PLUGIN_API int LoadChart(const char *chartPath) {
    // Loads a chart JSON file through ChartParser.
    if (!ChartParser::loadChart(chartPath, g_state.chart))
        return -1;
    if (g_adac != nullptr && (!loadSong(&g_state, chartPath) || !loadMetronome(&g_state, chartPath)))
        return -1;

    g_state.requestedSessionMode.store(SessionMode_Judge);
    ResetSessionTime();
    return 0;
}

extern "C" PLUGIN_API void ResetSessionTime(void) {
    // Resets the session-relative clock and judgment progress.
    g_state.nextNoteIndex.store(0);
    g_state.advanceFingeringTargetOnFailure = false;
    g_state.lastDetectedMidi = -1;
    g_state.gameStarted.store(false);
    g_state.summaryFinished.store(false);
    g_state.sessionMode.store(SessionMode_None);
    g_state.lastStreamTime.store(0.0);
    g_state.lastChartTimeMs.store(-COUNTDOWN_MS);
    g_state.sessionStreamTimeOffset.store(0.0);
    g_state.sessionClockStarted.store(false);
    g_state.isPaused.store(false);
    g_state.resumeRequested.store(false);
    g_state.lastRawStreamTime.store(0.0);
    g_state.pauseStartedStreamTime.store(0.0);
    g_state.pausedStreamDuration.store(0.0);
    g_state.audioQueue.readIndex.store(0);
    g_state.audioQueue.writeIndex.store(0);
    g_state.queuedAudioBlocks.store(0);
    g_state.processedAudioBlocks.store(0);
    g_state.pitchObservations.clear();
    g_state.pendingGuitarInputs.clear();
    g_state.pendingJudgments.clear();
    g_state.detectedOnsets.store(0);
    g_state.startedFingeringJudgments.store(0);
    g_state.passedChordJudgments.store(0);
    g_state.failedChordJudgments.store(0);
    g_state.lastGuitarInputEventAudioTimeMs.store(-g_state.guitarInputIntervalMs.load());
    if (g_state.onsetDetector)
        aubio_onset_reset(g_state.onsetDetector);
    preparePluginEventQueue(&g_state.eventQueue, 64);
    preparePluginEventQueue(&g_state.fingeringTestRawOnsetQueue, 64);
}

extern "C" PLUGIN_API int StartSession(void) {
    // Starts the audio stream and the plugin-owned judge thread.
    if (g_adac == nullptr || g_state.audioTestMode)
        return -1;
    if (g_adac->isStreamOpen() == false)
        return -1;

    ResetSessionTime();
    g_state.sessionMode.store(g_state.requestedSessionMode.load());
    g_state.stopRequested.store(false);
    g_state.droppedAudioBlocks.store(0);
    g_state.droppedJudgeEvents.store(0);

    g_judgeThread = std::thread(judgeThreadMain, &g_state);
    if (g_adac->startStream()) {
        StopSession();
        return -1;
    }
    return 0;
}

extern "C" PLUGIN_API int StartSlowPracticeSession(void) {
    // Starts chart judgment five seconds before the first note without song mixing.
    if (g_adac == nullptr || g_state.audioTestMode || g_adac->isStreamOpen() == false)
        return -1;

    ResetSessionTime();
    g_state.sessionMode.store(SessionMode_SlowPractice);
    g_state.lastChartTimeMs.store(g_state.chart.notes.front().startMs - COUNTDOWN_MS);
    g_state.stopRequested.store(false);
    g_state.droppedAudioBlocks.store(0);
    g_state.droppedJudgeEvents.store(0);

    g_judgeThread = std::thread(judgeThreadMain, &g_state);
    if (g_adac->startStream()) {
        StopSession();
        return -1;
    }
    return 0;
}

extern "C" PLUGIN_API void SetPracticeSpeed(float speed) {
    // Sets the chart-clock multiplier used by the running slow-practice session.
    if (speed < 0.25f)
        speed = 0.25f;
    if (speed > 1.25f)
        speed = 1.25f;
    g_state.practiceSpeed.store(speed);
}

extern "C" PLUGIN_API int StartFingeringPracticeSession(void) {
    // Starts note-by-note judgment without song playback or timing-based misses.
    if (g_adac == nullptr || g_state.audioTestMode || g_adac->isStreamOpen() == false)
        return -1;

    ResetSessionTime();
    g_state.sessionMode.store(SessionMode_FingeringPractice);
    g_state.stopRequested.store(false);
    g_state.droppedAudioBlocks.store(0);
    g_state.droppedJudgeEvents.store(0);

    g_judgeThread = std::thread(judgeThreadMain, &g_state);
    if (g_adac->startStream()) {
        StopSession();
        return -1;
    }
    return 0;
}

extern "C" PLUGIN_API void StopSession(void) {
    // Stops the audio stream and joins the judge thread.
    g_state.stopRequested.store(true);
    if (g_adac != nullptr && g_adac->isStreamRunning())
        g_adac->stopStream();
    if (g_judgeThread.joinable())
        g_judgeThread.join();
}

extern "C" PLUGIN_API void SetDSPParams(float inputGain, float outputGain, float lpfAlpha) {
    // Updates realtime monitor DSP parameters.
    g_state.inputGain.store(inputGain);
    g_state.outputGain.store(outputGain);
    g_state.lpfAlpha.store(lpfAlpha);
}

extern "C" PLUGIN_API void SetNamModelPath(const char *modelPath) {
    // Selects the NAM model file that the next audio initialization loads.
    g_state.namModelPath = modelPath;
}

extern "C" PLUGIN_API int StartAudioTest(void) {
    // Starts pass-through audio and RMS analysis on the judge thread.
    if (g_adac == nullptr || !g_state.audioTestMode ||
        g_adac->isStreamOpen() == false || g_adac->isStreamRunning())
        return -1;

    ResetSessionTime();
    g_state.sessionMode.store(SessionMode_AudioTest);
    g_state.stopRequested.store(false);
    g_state.droppedAudioBlocks.store(0);

    g_judgeThread = std::thread(judgeThreadMain, &g_state);
    if (g_adac->startStream()) {
        StopAudioTest();
        return -1;
    }
    return 0;
}

extern "C" PLUGIN_API void StopAudioTest(void) {
    // Stops pass-through audio and joins its RMS analysis thread.
    g_state.stopRequested.store(true);
    if (g_state.audioTestMode && g_adac != nullptr && g_adac->isStreamRunning())
        g_adac->stopStream();
    if (g_judgeThread.joinable())
        g_judgeThread.join();
    g_state.sessionMode.store(SessionMode_None);
    g_state.audioTestOutputLevelDb.store(-96.0f);
}

extern "C" PLUGIN_API int GetAudioTestOutputLevelDb(float *outLevelDb) {
    // Copies the latest judge-thread RMS level in dBFS for the client.
    if (g_adac == nullptr || !g_state.audioTestMode)
        return -1;

    *outLevelDb = g_state.audioTestOutputLevelDb.load();
    return 0;
}

extern "C" PLUGIN_API void PauseSession(void) {
    // Pauses audio output and judgment while preserving session progress.
    if (!g_state.isPaused.exchange(true))
        g_state.pauseStartedStreamTime.store(g_state.lastRawStreamTime.load());
}

extern "C" PLUGIN_API void ResumeSession(void) {
    // Resumes a paused session without advancing its session clock.
    if (g_state.isPaused.exchange(false))
        g_state.resumeRequested.store(true);
}

extern "C" PLUGIN_API int RestartSession(void) {
    // Restarts the active session mode with its clock and judgment progress reset.
    int sessionMode = g_state.sessionMode.load();
    StopSession();

    if (sessionMode == SessionMode_SlowPractice)
        return StartSlowPracticeSession();
    if (sessionMode == SessionMode_FingeringPractice)
        return StartFingeringPracticeSession();
    return StartSession();
}

extern "C" PLUGIN_API void SetSongVolume(float volume) {
    // Updates the chart-song volume without changing the instrument monitor gain.
    g_state.songVolume.store(volume);
}

extern "C" PLUGIN_API void SetGuitarInputIntervalMs(double intervalMs) {
    // Sets the minimum spacing between emitted guitar-control input events.
    g_state.guitarInputIntervalMs.store(intervalMs);
}

extern "C" PLUGIN_API int PollJudgeEvent(JudgeEvent *outEvent) {
    // Polls one pending judge event through the judge module.
    return pollJudgeEvent(&g_state, outEvent);
}

extern "C" PLUGIN_API int PollGuitarInputEvent(GuitarInputEvent *outEvent) {
    // Polls one pending guitar input event through the judge module.
    return pollGuitarInputEvent(&g_state, outEvent);
}

extern "C" PLUGIN_API int GetAudioStats(AudioStats *outStats) {
    // Copies current stream and judge progress stats for Unity.
    if (g_adac == nullptr)
        return -1;

    *outStats = {};
    outStats->streamTime = g_state.lastStreamTime.load();
    outStats->audioTimeMs = outStats->streamTime * 1000.0;
    outStats->chartTimeMs = g_state.lastChartTimeMs.load();
    outStats->countdownMs = COUNTDOWN_MS;
    outStats->streamLatency = g_adac->isStreamOpen() ? g_adac->getStreamLatency() : 0;
    outStats->bufferFrames = g_state.bufferFrames;
    outStats->droppedAudioBlocks = g_state.droppedAudioBlocks.load();
    outStats->droppedJudgeEvents = g_state.droppedJudgeEvents.load();
    outStats->totalNotes = (int)g_state.chart.notes.size();
    outStats->nextNoteIndex = g_state.nextNoteIndex.load();
    outStats->isRunning = g_adac->isStreamRunning() ? 1 : 0;
    outStats->isFinished = g_state.summaryFinished.load() ? 1 : 0;
    outStats->isPaused = g_state.isPaused.load() ? 1 : 0;
    return 0;
}

extern "C" PLUGIN_API int GetJudgmentDiagnostics(JudgmentDiagnostics *outDiagnostics) {
    // Copies counters used to diagnose fingering-practice onset and chord judgment behavior.
    if (g_adac == nullptr && g_state.input == nullptr)
        return -1;

    *outDiagnostics = {};
    outDiagnostics->detectedOnsets = g_state.detectedOnsets.load();
    outDiagnostics->startedFingeringJudgments = g_state.startedFingeringJudgments.load();
    outDiagnostics->passedChordJudgments = g_state.passedChordJudgments.load();
    outDiagnostics->failedChordJudgments = g_state.failedChordJudgments.load();
    return 0;
}

extern "C" PLUGIN_API int PollFingeringTestRawOnset(FingeringTestRawOnset *outOnset) {
    // Polls one raw onset recorded by the no-device fingering test path.
    return pollFingeringTestRawOnset(&g_state, outOnset);
}

extern "C" PLUGIN_API int StartFingeringTestSession(void) {
    // Starts fingering judgment without a device stream for recorded test audio.
    if (g_adac != nullptr || g_state.input == nullptr)
        return -1;

    ResetSessionTime();
    g_state.sessionMode.store(SessionMode_FingeringPractice);
    g_state.advanceFingeringTargetOnFailure = true;
    g_state.stopRequested.store(false);
    g_state.droppedAudioBlocks.store(0);
    g_state.droppedJudgeEvents.store(0);
    g_judgeThread = std::thread(judgeThreadMain, &g_state);
    return 0;
}

extern "C" PLUGIN_API int StartJudgeTestSession(void) {
    // Starts the regular time-based judge thread without opening an audio stream.
    if (g_adac != nullptr || g_state.input == nullptr)
        return -1;

    ResetSessionTime();
    g_state.sessionMode.store(SessionMode_Judge);
    g_state.stopRequested.store(false);
    g_state.droppedAudioBlocks.store(0);
    g_state.droppedJudgeEvents.store(0);
    g_judgeThread = std::thread(judgeThreadMain, &g_state);
    return 0;
}

extern "C" PLUGIN_API int FeedFingeringTestAudio(const int16_t *samples, unsigned int frames) {
    // Sends one recorded audio block through the callback-to-judge queue used in practice.
    if (g_state.sessionMode.load() != SessionMode_FingeringPractice || frames != g_state.bufferFrames)
        return -1;

    double streamTime = g_state.lastStreamTime.load();
    int noteIndex = g_state.nextNoteIndex.load();
    double chartTimeMs = noteIndex < (int)g_state.chart.notes.size()
                             ? g_state.chart.notes[noteIndex].startMs
                             : g_state.chart.durationMs;
    if (!pushAudioBlock(&g_state.audioQueue,
                        const_cast<MY_TYPE *>(samples),
                        frames,
                        frames * g_state.channels,
                        streamTime,
                        chartTimeMs,
                        0.0)) {
        g_state.droppedAudioBlocks.fetch_add(1);
        return -1;
    }

    g_state.queuedAudioBlocks.fetch_add(1);
    g_state.lastStreamTime.store(streamTime + (double)frames / g_state.sampleRate);
    g_state.lastChartTimeMs.store(chartTimeMs);
    return 0;
}

extern "C" PLUGIN_API int FeedJudgeTestAudio(const int16_t *samples, unsigned int frames) {
    // Queues recorded PCM with its audio time used directly as the chart clock.
    if (g_state.sessionMode.load() != SessionMode_Judge || frames != g_state.bufferFrames)
        return -1;

    double streamTime = g_state.lastStreamTime.load();
    double chartTimeMs = streamTime * 1000.0;
    if (!pushAudioBlock(&g_state.audioQueue,
                        const_cast<MY_TYPE *>(samples),
                        frames,
                        frames * g_state.channels,
                        streamTime,
                        chartTimeMs,
                        1.0)) {
        g_state.droppedAudioBlocks.fetch_add(1);
        return -1;
    }

    g_state.queuedAudioBlocks.fetch_add(1);
    g_state.lastStreamTime.store(streamTime + (double)frames / g_state.sampleRate);
    g_state.lastChartTimeMs.store(chartTimeMs);
    return 0;
}

extern "C" PLUGIN_API void SetIgnoreOnsetsDuringJudgmentCollection(int enabled) {
    // Selects whether one pending pitch or chord judgment locks out new onsets.
    g_state.ignoreOnsetsDuringJudgmentCollection.store(enabled != 0);
}

extern "C" PLUGIN_API int GetJudgeProcessingStats(JudgeProcessingStats *outStats) {
    // Copies callback-to-judge audio block processing counters.
    if (g_adac == nullptr)
        return -1;

    *outStats = {};
    outStats->queuedAudioBlocks = g_state.queuedAudioBlocks.load();
    outStats->processedAudioBlocks = g_state.processedAudioBlocks.load();
    outStats->droppedAudioBlocks = g_state.droppedAudioBlocks.load();
    return 0;
}

extern "C" PLUGIN_API int GetSongSyncInfo(SongSyncInfo *outInfo) {
    // Returns the chart song and its position on the plugin session clock.
    if (g_adac == nullptr || g_state.chart.audioFile.empty())
        return -1;

    *outInfo = {};
    std::strncpy(
        outInfo->audioFile, g_state.chart.audioFile.c_str(), sizeof(outInfo->audioFile) - 1);
    outInfo->audioOffsetMs = g_state.chart.audioOffsetMs;
    outInfo->durationMs = g_state.chart.durationMs;
    outInfo->songTimeMs = g_state.lastChartTimeMs.load();
    return 0;
}

extern "C" PLUGIN_API void Shutdown(void) {
    // Releases the stream, selected ASIO driver, judge thread, and aubio resources.
    StopSession();
    if (g_adac != nullptr) {
        if (g_adac->isStreamOpen())
            g_adac->closeStream();
        delete g_adac;
        g_adac = nullptr;
    }
    releaseAubio(&g_state);
    g_state.namModel.reset();
    g_state.namInput.clear();
    g_state.namOutput.clear();
    releaseSelectedAsioDriver();
    shutdownSongDecoder();
    aubio_cleanup();
}
