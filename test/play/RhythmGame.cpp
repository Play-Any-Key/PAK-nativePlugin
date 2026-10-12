#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
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

struct RhythmState {
    ChartParser::Chart chart;
    unsigned int channels;
    unsigned int bufferBytes;
    fvec_t *input;
    fvec_t *pitch;
    fvec_t *onset;
    aubio_pitch_t *pitchDetector;
    aubio_onset_t *onsetDetector;
    std::string lastResult;
    std::vector<std::string> noteResults;
    double nextPrintTime;
    int nextNoteIndex;
    int lastDetectedMidi;
    bool gameStarted;
    bool summaryPrinted;
};

std::string formatSeconds(double seconds) {
    // Formats seconds for the console HUD.
    std::ostringstream text;
    text << std::fixed << std::setprecision(2) << seconds << "s";
    return text.str();
}

std::string makeProgressBar(double streamTime, int durationMs) {
    // Builds a small progress bar for the current chart position.
    const int width = 12;
    double durationSeconds = durationMs / 1000.0;
    double ratio = durationSeconds > 0.0 ? streamTime / durationSeconds : 0.0;
    if (ratio < 0.0)
        ratio = 0.0;
    if (ratio > 1.0)
        ratio = 1.0;

    int filled = (int)std::round(ratio * width);
    std::string bar = "[";
    for (int i = 0; i < width; i++) bar += i < filled ? '#' : '-';
    bar += "]";
    return bar;
}

std::string shortenText(const std::string &text, int maxLength) {
    // Shortens long judgment text so the HUD stays on one console line.
    if ((int)text.size() <= maxLength)
        return text;
    return text.substr(0, maxLength - 3) + "...";
}

std::string makeTimingCue(double remainSeconds) {
    // Shows the last second before a note as four 0.25 second boxes.
    int filled = (int)std::floor((1.0 - remainSeconds) / 0.25) + 1;
    if (remainSeconds > 1.0)
        filled = 0;
    if (filled < 0)
        filled = 0;
    if (filled > 4)
        filled = 4;

    std::string cue;
    for (int i = 0; i < 4; i++)
        cue += i < filled ? "[#]" : "[ ]";
    return cue;
}

const char *judgeTiming(double absErrorMs) {
    // Converts timing error to a rhythm judgment.
    if (absErrorMs <= PERFECT_MS)
        return "Perfect";
    if (absErrorMs <= GOOD_MS)
        return "Good";
    if (absErrorMs <= BAD_MS)
        return "Bad";
    return "Miss";
}

void printHud(RhythmState *state, double streamTime) {
    // Prints the current progress and next note as a single updating line.
    std::cout << "\r" << makeProgressBar(streamTime, state->chart.durationMs) << " "
              << formatSeconds(streamTime) << "/"
              << formatSeconds(state->chart.durationMs / 1000.0);

    if (state->nextNoteIndex < (int)state->chart.notes.size()) {
        const ChartParser::ChartNote &note = state->chart.notes[state->nextNoteIndex];
        double remainSeconds = note.startMs / 1000.0 - streamTime;
        std::cout << " | N " << state->nextNoteIndex + 1 << "/" << state->chart.notes.size() << " S"
                  << note.stringNumber << " F" << note.fret << " " << note.noteName << " "
                  << makeTimingCue(remainSeconds);
    } else {
        std::cout << " | N finished";
    }

    std::cout << " | Last " << shortenText(state->lastResult, 32) << std::flush;
}

void printCountdown(double gameTime) {
    // Prints the pre-song countdown as a single updating line.
    int count = (int)std::ceil(-gameTime);
    std::cout << "\rStarting in " << count << "..." << std::flush;
}

void printSummary(RhythmState *state) {
    // Prints every note judgment after the chart is finished.
    std::cout << "\n\nResult\n";
    for (int i = 0; i < (int)state->chart.notes.size(); i++) {
        const ChartParser::ChartNote &note = state->chart.notes[i];
        std::cout << std::setw(2) << i + 1 << ". "
                  << "string " << note.stringNumber << ", fret " << note.fret << ", "
                  << note.noteName << " @ " << formatSeconds(note.startMs / 1000.0) << " -> "
                  << state->noteResults[i] << "\n";
    }
    std::cout << std::flush;
}

void usage(void) {
    // Command-line usage for the rhythm game test.
    std::cout << "\nuseage: RhythmGame N fs <iDevice> <oDevice> <iChannelOffset> <oChannelOffset> "
                 "<chartPath>\n";
    std::cout << "    where N = number of channels,\n";
    std::cout << "    fs = the sample rate,\n";
    std::cout << "    iDevice = optional input device index to use (default = 0),\n";
    std::cout << "    oDevice = optional output device index to use (default = 0),\n";
    std::cout << "    iChannelOffset = an optional input channel offset (default = 0),\n";
    std::cout << "    oChannelOffset = optional output channel offset (default = 0),\n";
    std::cout << "    and chartPath = optional chart json path.\n\n";
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

int inoutRhythmGame(void *outputBuffer,
                    void *inputBuffer,
                    unsigned int nBufferFrames,
                    double streamTime,
                    RtAudioStreamStatus status,
                    void *data) {
    // Copies live input to output and judges chart note timing and pitch.
    if (status)
        std::cout << "Stream over/underflow detected." << std::endl;

    RhythmState *state = (RhythmState *)data;
    memcpy(outputBuffer, inputBuffer, state->bufferBytes);
    double gameTime = streamTime - COUNTDOWN_SECONDS;

    if (gameTime < 0.0) {
        if (streamTime >= state->nextPrintTime) {
            printCountdown(gameTime);
            state->nextPrintTime += 0.05;
        }
        return 0;
    }

    if (!state->gameStarted) {
        state->gameStarted = true;
        state->nextPrintTime = streamTime;
        printHud(state, 0.0);
    }

    MY_TYPE *samples = (MY_TYPE *)inputBuffer;
    for (unsigned int i = 0; i < nBufferFrames; i++) {
        smpl_t sample = (smpl_t)samples[i * state->channels] / 32768.0f;
        fvec_set_sample(state->input, sample, i);
    }

    aubio_pitch_do(state->pitchDetector, state->input, state->pitch);
    state->lastDetectedMidi = (int)std::round(fvec_get_sample(state->pitch, 0));

    aubio_onset_do(state->onsetDetector, state->input, state->onset);
    if (fvec_get_sample(state->onset, 0) != 0.0f &&
        state->nextNoteIndex < (int)state->chart.notes.size()) {
        double onsetMs = aubio_onset_get_last_s(state->onsetDetector) * 1000.0;
        const ChartParser::ChartNote &note = state->chart.notes[state->nextNoteIndex];
        double errorMs = onsetMs - note.startMs;
        double absErrorMs = std::abs(errorMs);
        const char *result = judgeTiming(absErrorMs);
        bool pitchMatched = std::abs(state->lastDetectedMidi - note.midi) <= PITCH_TOLERANCE;

        std::ostringstream resultText;
        if (absErrorMs <= BAD_MS && pitchMatched) {
            resultText << result << " (" << std::showpos << std::fixed << std::setprecision(1)
                       << errorMs << std::noshowpos << " ms, " << note.noteName << ")";
            state->noteResults[state->nextNoteIndex] = resultText.str();
            state->nextNoteIndex++;
        } else if (absErrorMs <= BAD_MS) {
            resultText << "Miss (wrong note: MIDI " << state->lastDetectedMidi << ", target "
                       << note.noteName << ")";
            state->noteResults[state->nextNoteIndex] = resultText.str();
            state->nextNoteIndex++;
        } else {
            resultText << "Miss (" << std::showpos << std::fixed << std::setprecision(1) << errorMs
                       << std::noshowpos << " ms)";
        }

        state->lastResult = resultText.str();
        printHud(state, gameTime);
    }

    while (state->nextNoteIndex < (int)state->chart.notes.size()) {
        const ChartParser::ChartNote &note = state->chart.notes[state->nextNoteIndex];
        if (gameTime * 1000.0 <= note.startMs + BAD_MS)
            break;

        std::ostringstream resultText;
        resultText << "Miss (no input: " << note.noteName << ")";
        state->lastResult = resultText.str();
        state->noteResults[state->nextNoteIndex] = resultText.str();
        state->nextNoteIndex++;
        printHud(state, gameTime);
    }

    if (state->nextNoteIndex >= (int)state->chart.notes.size() && !state->summaryPrinted) {
        state->summaryPrinted = true;
        printHud(state, gameTime);
        printSummary(state);
        return 0;
    }

    if (!state->summaryPrinted && streamTime >= state->nextPrintTime) {
        printHud(state, gameTime);
        state->nextPrintTime += 0.05;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    unsigned int channels, fs, oDevice = 0, iDevice = 0, iOffset = 0, oOffset = 0;
    std::string chartPath = "assets/charts/simple_chromatic_001.json";

    // Minimal command-line checking.
    if (argc < 3 || argc > 8)
        usage();

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
    if (argc > 7)
        chartPath = argv[7];

    RhythmState state = {};
    if (!ChartParser::loadChart(chartPath, state.chart)) {
        std::cout << "Failed to load chart: " << chartPath << std::endl;
        return 1;
    }

    RtAudio adac;
    std::vector<unsigned int> deviceIds = adac.getDeviceIds();
    if (deviceIds.size() < 1) {
        std::cout << "\nNo audio devices found!\n";
        exit(1);
    }

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
    state.channels = channels;
    state.bufferBytes = bufferFrames * channels * sizeof(MY_TYPE);
    state.lastResult = "Waiting";
    state.nextPrintTime = 0.05;
    state.nextNoteIndex = 0;
    state.lastDetectedMidi = -1;
    state.gameStarted = false;
    state.summaryPrinted = false;
    state.noteResults.assign(state.chart.notes.size(), "Not judged");

    if (adac.openStream(&oParams,
                        &iParams,
                        FORMAT,
                        fs,
                        &bufferFrames,
                        &inoutRhythmGame,
                        (void *)&state,
                        &options)) {
        goto cleanup;
    }

    if (adac.isStreamOpen() == false)
        goto cleanup;

    state.bufferBytes = bufferFrames * channels * sizeof(MY_TYPE);
    state.input = new_fvec(bufferFrames);
    state.pitch = new_fvec(1);
    state.onset = new_fvec(1);
    state.pitchDetector = new_aubio_pitch("default", 2048, bufferFrames, fs);
    state.onsetDetector = new_aubio_onset("default", 1024, bufferFrames, fs);
    aubio_pitch_set_unit(state.pitchDetector, "midi");
    aubio_onset_set_threshold(state.onsetDetector, 0.3f);

    std::cout << "\nLoaded chart: " << state.chart.title << " / " << state.chart.difficulty << "\n";
    std::cout << "Stream latency = " << adac.getStreamLatency() << " frames" << std::endl;
    std::cout << "Running ... press <enter> to quit (buffer frames = " << bufferFrames << ").\n";

    if (adac.startStream())
        goto cleanup;

    char input;
    std::cin.get(input);

cleanup:
    std::cout << std::endl;
    if (adac.isStreamRunning())
        adac.stopStream();
    if (adac.isStreamOpen())
        adac.closeStream();
    if (state.pitchDetector)
        del_aubio_pitch(state.pitchDetector);
    if (state.onsetDetector)
        del_aubio_onset(state.onsetDetector);
    if (state.pitch)
        del_fvec(state.pitch);
    if (state.onset)
        del_fvec(state.onset);
    if (state.input)
        del_fvec(state.input);
    aubio_cleanup();

    return 0;
}
