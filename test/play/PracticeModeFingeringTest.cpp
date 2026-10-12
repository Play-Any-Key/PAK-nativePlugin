#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "../../src/ChartParser.h"
#include "../../src/main.h"

#ifdef _WIN32
#include <conio.h>
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
    int (*StartFingeringPracticeSession)(void);
    void (*StopSession)(void);
    int (*PollJudgeEvent)(JudgeEvent *);
    int (*GetAudioStats)(AudioStats *);
    int (*GetJudgmentDiagnostics)(JudgmentDiagnostics *);
    void (*Shutdown)(void);
};

void usage(void) {
    // Prints the command-line arguments accepted by this fingering-practice test.
    std::cout << "usage: PracticeModeFingeringTest N fs <iDevice> <oDevice> <iChannelOffset> "
                 "<oChannelOffset> <chartPath> <dllPath>\n";
    std::exit(0);
}

template <typename FunctionType>
bool loadPluginFunction(PluginApi *plugin, const char *name, FunctionType *outFunction) {
    // Loads one exported function from the native plugin DLL.
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
    // Opens the DLL and binds the APIs required by the fingering-practice loop.
#ifdef _WIN32
    *plugin = {};
    plugin->module = LoadLibraryA(dllPath.c_str());
    if (plugin->module == nullptr)
        return false;

    return loadPluginFunction(plugin, "Initialize", &plugin->Initialize) &&
           loadPluginFunction(plugin, "LoadChart", &plugin->LoadChart) &&
           loadPluginFunction(
               plugin, "StartFingeringPracticeSession", &plugin->StartFingeringPracticeSession) &&
           loadPluginFunction(plugin, "StopSession", &plugin->StopSession) &&
           loadPluginFunction(plugin, "PollJudgeEvent", &plugin->PollJudgeEvent) &&
           loadPluginFunction(plugin, "GetAudioStats", &plugin->GetAudioStats) &&
           loadPluginFunction(plugin, "GetJudgmentDiagnostics", &plugin->GetJudgmentDiagnostics) &&
           loadPluginFunction(plugin, "Shutdown", &plugin->Shutdown);
#else
    (void)dllPath;
    (void)plugin;
    return false;
#endif
}

void unloadPlugin(PluginApi *plugin) {
    // Closes the DLL after the plugin has released its resources.
#ifdef _WIN32
    if (plugin->module != nullptr)
        FreeLibrary(plugin->module);
#else
    (void)plugin;
#endif
}

bool enterPressed(void) {
    // Checks whether the user pressed enter without blocking the practice loop.
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

void printTargetNote(const ChartParser::Chart &chart, int noteIndex) {
    // Displays the note that must be played before the practice session can advance.
    if (noteIndex >= (int)chart.notes.size()) {
        std::cout << "\nAll notes completed.\n";
        return;
    }

    const ChartParser::ChartNote &note = chart.notes[noteIndex];
    std::cout << "\n\nTarget " << noteIndex + 1 << "/" << chart.notes.size() << ": ";
    if (note.interpretation == "chord") {
        std::cout << "Chord " << note.noteName;
    } else {
        std::cout << "String " << note.stringNumber << ", fret " << note.fret << " ("
                  << note.noteName << ")";
    }
    std::cout << "\nPlay the target to continue.\n" << std::flush;
}

const char *judgeResultText(int result) {
    // Converts one native judge result to text for the console.
    return result == JudgeResult_Perfect ? "Correct" : "Incorrect";
}

int main(int argc, char *argv[]) {
    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    unsigned int inputDevice = 0;
    unsigned int outputDevice = 0;
    unsigned int inputOffset = 0;
    unsigned int outputOffset = 0;
    std::string chartPath = "assets/charts/PAK - Night.json";
    std::string dllPath = "out/build/ninja-debug/PAKNativePlugin.dll";

    if (argc < 3 || argc > 9)
        usage();

    channels = (unsigned int)std::atoi(argv[1]);
    sampleRate = (unsigned int)std::atoi(argv[2]);
    if (argc > 3)
        inputDevice = (unsigned int)std::atoi(argv[3]);
    if (argc > 4)
        outputDevice = (unsigned int)std::atoi(argv[4]);
    if (argc > 5)
        inputOffset = (unsigned int)std::atoi(argv[5]);
    if (argc > 6)
        outputOffset = (unsigned int)std::atoi(argv[6]);
    if (argc > 7)
        chartPath = argv[7];
    if (argc > 8)
        dllPath = argv[8];

    ChartParser::Chart chart = {};
    if (!ChartParser::loadChart(chartPath, chart)) {
        std::cout << "Failed to load chart: " << chartPath << "\n";
        return 1;
    }

    PluginApi plugin = {};
    if (!loadPlugin(dllPath, &plugin)) {
        std::cout << "Failed to load plugin: " << dllPath << "\n";
        unloadPlugin(&plugin);
        return 1;
    }

    int result = 0;
    int shownNoteIndex = -1;
    unsigned int consumedJudgeEvents = 0;
    unsigned int previousDetectedOnsets = 0;
    unsigned int previousStartedFingeringJudgments = 0;
    std::chrono::steady_clock::time_point lastQueueReportAt = std::chrono::steady_clock::now();
    AudioStats stats = {};
    if (plugin.Initialize(
            channels, sampleRate, inputDevice, outputDevice, inputOffset, outputOffset) != 0 ||
        plugin.LoadChart(chartPath.c_str()) != 0) {
        std::cout << "Failed to initialize fingering practice session.\n";
        result = 1;
        goto cleanup;
    }

    std::cout << "Fingering practice: song output disabled\n";
    std::cout << "The target stays on screen until it is played correctly.\n";
    if (plugin.StartFingeringPracticeSession() != 0) {
        std::cout << "Failed to start fingering practice session.\n";
        result = 1;
        goto cleanup;
    }

    while (!enterPressed()) {
        stats = {};
        if (plugin.GetAudioStats(&stats) != 0)
            break;

        if (stats.nextNoteIndex != shownNoteIndex) {
            shownNoteIndex = stats.nextNoteIndex;
            printTargetNote(chart, shownNoteIndex);
        }

        JudgeEvent event = {};
        while (plugin.PollJudgeEvent(&event) == 1) {
            consumedJudgeEvents++;
            std::cout << judgeResultText(event.result) << ": " << event.noteName;
            if (event.result == JudgeResult_Miss)
                std::cout << " - retry the same target";
            std::cout << "\n" << std::flush;
        }

        auto now = std::chrono::steady_clock::now();
        if (now - lastQueueReportAt >= std::chrono::seconds(1)) {
            JudgmentDiagnostics diagnostics = {};
            if (plugin.GetJudgmentDiagnostics(&diagnostics) != 0) {
                result = 1;
                goto cleanup;
            }
            std::cout << "Onset | detected " << diagnostics.detectedOnsets << " (+"
                      << diagnostics.detectedOnsets - previousDetectedOnsets << ") | accepted "
                      << diagnostics.startedFingeringJudgments << " (+"
                      << diagnostics.startedFingeringJudgments - previousStartedFingeringJudgments
                      << ")\n"
                      << std::flush;
            previousDetectedOnsets = diagnostics.detectedOnsets;
            previousStartedFingeringJudgments = diagnostics.startedFingeringJudgments;
            lastQueueReportAt = now;
        }

        if (stats.isFinished)
            break;

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

cleanup:
    plugin.StopSession();
    plugin.Shutdown();
    unloadPlugin(&plugin);
    return result;
}
