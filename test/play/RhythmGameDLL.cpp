#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../../src/ChartParser.h"
#include "../../src/main.h"

#ifdef _WIN32
#include <conio.h>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

struct PluginApi {
#ifdef _WIN32
    HMODULE module;
#endif
    int (*Initialize)(
        unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
    int (*LoadChart)(const char *);
    int (*StartSession)(void);
    void (*StopSession)(void);
    void (*SetDSPParams)(float, float, float);
    int (*PollJudgeEvent)(JudgeEvent *);
    int (*GetAudioStats)(AudioStats *);
    int (*GetSongSyncInfo)(SongSyncInfo *);
    void (*Shutdown)(void);
};

struct ClientState {
    ChartParser::Chart chart;
    std::vector<std::string> noteResults;
    std::string lastResult;
    int nextNoteIndex;
    bool summaryPrinted;
};

std::string formatSeconds(double seconds) {
    // Formats seconds for the console HUD.
    std::ostringstream text;
    text << std::fixed << std::setprecision(2) << seconds << "s";
    return text.str();
}

std::string makeProgressBar(double chartTimeMs, int durationMs) {
    // Builds a small progress bar for the current chart position.
    const int width = 12;
    double ratio = durationMs > 0 ? chartTimeMs / durationMs : 0.0;
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
    for (int i = 0; i < 4; i++) cue += i < filled ? "[#]" : "[ ]";
    return cue;
}

const char *judgeResultText(int result) {
    // Converts plugin judge result code to display text.
    if (result == JudgeResult_Perfect)
        return "Perfect";
    if (result == JudgeResult_Good)
        return "Good";
    if (result == JudgeResult_Bad)
        return "Bad";
    if (result == JudgeResult_Wrong)
        return "Wrong";
    return "Miss";
}

std::string formatJudgeEvent(const JudgeEvent &event) {
    // Builds one display string from a plugin judge event.
    std::ostringstream text;
    text << judgeResultText(event.result) << " (" << std::showpos << std::fixed
         << std::setprecision(1) << event.errorMs << std::noshowpos << " ms, " << event.noteName;
    if (event.result == JudgeResult_Wrong)
        text << ", MIDI " << event.detectedMidi << "/" << event.targetMidi;
    text << ")";
    return text.str();
}

void printCountdown(double chartTimeMs) {
    // Prints the pre-song countdown using plugin chart time.
    int count = (int)std::ceil(-chartTimeMs / 1000.0);
    std::cout << "\rStarting in " << count << "..." << std::flush;
}

void printHud(ClientState *state, const AudioStats &stats) {
    // Prints current client HUD using plugin chart time as the master clock.
    std::cout << "\r" << makeProgressBar(stats.chartTimeMs, state->chart.durationMs) << " "
              << formatSeconds(std::max(stats.chartTimeMs, 0.0) / 1000.0) << "/"
              << formatSeconds(state->chart.durationMs / 1000.0);

    if (state->nextNoteIndex < (int)state->chart.notes.size()) {
        const ChartParser::ChartNote &note = state->chart.notes[state->nextNoteIndex];
        double remainSeconds = (note.startMs - stats.chartTimeMs) / 1000.0;
        std::cout << " | N " << state->nextNoteIndex + 1 << "/" << state->chart.notes.size() << " ";
        if (note.interpretation == "chord")
            std::cout << note.noteName << " ";
        else
            std::cout << "S" << note.stringNumber << " F" << note.fret << " " << note.noteName
                      << " ";
        std::cout << makeTimingCue(remainSeconds);
    } else {
        std::cout << " | N finished";
    }

    std::cout << " | Last " << shortenText(state->lastResult, 32) << " | P " << stats.nextNoteIndex
              << "/" << stats.totalNotes << " DA " << stats.droppedAudioBlocks << " DE "
              << stats.droppedJudgeEvents << std::flush;
}

void printSummary(ClientState *state) {
    // Prints every plugin judgment after the chart is finished.
    std::cout << "\n\nResult\n";
    for (int i = 0; i < (int)state->chart.notes.size(); i++) {
        const ChartParser::ChartNote &note = state->chart.notes[i];
        std::cout << std::setw(2) << i + 1 << ". ";
        if (note.interpretation == "chord")
            std::cout << note.noteName;
        else
            std::cout << "string " << note.stringNumber << ", fret " << note.fret << ", "
                      << note.noteName;
        std::cout << " @ " << formatSeconds(note.startMs / 1000.0) << " -> "
                  << state->noteResults[i] << "\n";
    }
    std::cout << std::flush;
}

void usage(void) {
    // Command-line usage for the DLL-backed rhythm game test.
    std::cout << "\nuseage: RhythmGameDLL N fs <iDevice> <oDevice> <iChannelOffset> "
                 "<oChannelOffset> <chartPath> <dllPath>\n";
    std::cout << "    where N = number of channels,\n";
    std::cout << "    fs = the sample rate,\n";
    std::cout << "    iDevice = optional input device index to use (default = 0),\n";
    std::cout << "    oDevice = optional output device index to use (default = 0),\n";
    std::cout << "    iChannelOffset = an optional input channel offset (default = 0),\n";
    std::cout << "    oChannelOffset = optional output channel offset (default = 0),\n";
    std::cout << "    chartPath = optional chart json path,\n";
    std::cout << "    and dllPath = optional PAKNativePlugin.dll path.\n\n";
    exit(0);
}

bool enterPressed(void) {
    // Checks whether the user pressed enter without blocking the client loop.
#ifdef _WIN32
    if (_kbhit()) {
        int input = _getch();
        return input == '\r' || input == '\n';
    }
    return false;
#else
    if (std::cin.rdbuf()->in_avail() > 0) {
        char input;
        std::cin.get(input);
        return input == '\r' || input == '\n';
    }
    return false;
#endif
}

template <typename FunctionType>
bool loadPluginFunction(PluginApi *plugin, const char *name, FunctionType *outFunction) {
    // Loads one exported plugin function by name.
#ifdef _WIN32
    *outFunction = reinterpret_cast<FunctionType>(GetProcAddress(plugin->module, name));
    return *outFunction != nullptr;
#else
    (void)plugin;
    (void)name;
    (void)outFunction;
    return false;
#endif
}

bool loadPlugin(const std::string &dllPath, PluginApi *plugin) {
    // Loads PAKNativePlugin.dll and binds the C API entry points.
#ifdef _WIN32
    *plugin = {};
    plugin->module = LoadLibraryA(dllPath.c_str());
    if (plugin->module == nullptr)
        return false;

    if (!loadPluginFunction(plugin, "Initialize", &plugin->Initialize))
        return false;
    if (!loadPluginFunction(plugin, "LoadChart", &plugin->LoadChart))
        return false;
    if (!loadPluginFunction(plugin, "StartSession", &plugin->StartSession))
        return false;
    if (!loadPluginFunction(plugin, "StopSession", &plugin->StopSession))
        return false;
    if (!loadPluginFunction(plugin, "SetDSPParams", &plugin->SetDSPParams))
        return false;
    if (!loadPluginFunction(plugin, "PollJudgeEvent", &plugin->PollJudgeEvent))
        return false;
    if (!loadPluginFunction(plugin, "GetAudioStats", &plugin->GetAudioStats))
        return false;
    if (!loadPluginFunction(plugin, "GetSongSyncInfo", &plugin->GetSongSyncInfo))
        return false;
    if (!loadPluginFunction(plugin, "Shutdown", &plugin->Shutdown))
        return false;

    return true;
#else
    (void)dllPath;
    (void)plugin;
    return false;
#endif
}

void unloadPlugin(PluginApi *plugin) {
    // Releases the loaded plugin module.
#ifdef _WIN32
    if (plugin->module != nullptr) {
        FreeLibrary(plugin->module);
        plugin->module = nullptr;
    }
#else
    (void)plugin;
#endif
}

void applyJudgeEvent(ClientState *state, const JudgeEvent &event) {
    // Applies one plugin event to the client-owned display chart.
    if (event.noteIndex < 0 || event.noteIndex >= (int)state->noteResults.size())
        return;

    std::string resultText = formatJudgeEvent(event);
    state->noteResults[event.noteIndex] = resultText;
    state->lastResult = resultText;
    state->nextNoteIndex = std::max(state->nextNoteIndex, event.noteIndex + 1);
}

int main(int argc, char *argv[]) {
    unsigned int channels, fs, oDevice = 0, iDevice = 0, iOffset = 0, oOffset = 0;
    std::string chartPath = "assets/charts/PAK - Night.json";
#ifdef PAK_DEFAULT_DLL_PATH
    std::string dllPath = PAK_DEFAULT_DLL_PATH;
#else
    std::string dllPath = "PAKNativePlugin.dll";
#endif

    // Minimal command-line checking.
    if (argc < 3 || argc > 9)
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
    if (argc > 8)
        dllPath = argv[8];

    ClientState state = {};
    state.lastResult = "Waiting";
    state.nextNoteIndex = 0;
    state.summaryPrinted = false;

    if (!ChartParser::loadChart(chartPath, state.chart)) {
        std::cout << "Failed to load client chart: " << chartPath << std::endl;
        return 1;
    }
    state.noteResults.assign(state.chart.notes.size(), "Not judged");

    PluginApi plugin = {};
    if (!loadPlugin(dllPath, &plugin)) {
        std::cout << "Failed to load plugin: " << dllPath << std::endl;
        unloadPlugin(&plugin);
        return 1;
    }

    int result = 0;
    SongSyncInfo songInfo = {};
    std::chrono::steady_clock::time_point lastSyncCheck = {};
    if (plugin.Initialize(channels, fs, iDevice, oDevice, iOffset, oOffset) != 0) {
        std::cout << "Plugin Initialize failed." << std::endl;
        result = 1;
        goto cleanup;
    }

    if (plugin.LoadChart(chartPath.c_str()) != 0) {
        std::cout << "Plugin LoadChart failed: " << chartPath << std::endl;
        result = 1;
        goto cleanup;
    }

    if (plugin.GetSongSyncInfo(&songInfo) != 0) {
        std::cout << "Plugin GetSongSyncInfo failed." << std::endl;
        result = 1;
        goto cleanup;
    }

    plugin.SetDSPParams(4.0f, 0.5f, 0.2f);

    std::cout << "\nLoaded client chart: " << state.chart.title << " / " << state.chart.difficulty
              << "\n";
    std::cout << "Loaded plugin DLL : " << dllPath << "\n";
    std::cout << "Chart audio       : " << songInfo.audioFile << "\n";
    std::cout << "Song playback     : native DLL output\n";
    std::cout << "Running ... press <enter> to quit.\n";

    if (plugin.StartSession() != 0) {
        std::cout << "Plugin StartSession failed." << std::endl;
        result = 1;
        goto cleanup;
    }

    lastSyncCheck = std::chrono::steady_clock::now();
    while (!enterPressed()) {
        AudioStats stats = {};
        if (plugin.GetAudioStats(&stats) != 0)
            break;

        if (plugin.GetSongSyncInfo(&songInfo) != 0)
            break;

        auto now = std::chrono::steady_clock::now();
        if (now - lastSyncCheck >= std::chrono::milliseconds(500)) {
            std::cout << "\nSong time: " << (int)songInfo.songTimeMs << " ms (native DLL output)"
                      << std::flush;
            lastSyncCheck = now;
        }

        JudgeEvent event = {};
        while (plugin.PollJudgeEvent(&event)) applyJudgeEvent(&state, event);

        if (stats.chartTimeMs < 0.0) {
            printCountdown(stats.chartTimeMs);
        } else if (!state.summaryPrinted && stats.isFinished) {
            printHud(&state, stats);
            printSummary(&state);
            state.summaryPrinted = true;
        } else if (!state.summaryPrinted) {
            printHud(&state, stats);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

cleanup:
    std::cout << std::endl;
    if (plugin.StopSession)
        plugin.StopSession();
    if (plugin.Shutdown)
        plugin.Shutdown();
    unloadPlugin(&plugin);
    return result;
}
