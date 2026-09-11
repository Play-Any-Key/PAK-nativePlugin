#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

#include "../../src/main.h"

#ifdef _WIN32
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
    int (*StartSlowPracticeSession)(void);
    void (*SetPracticeSpeed)(float);
    void (*StopSession)(void);
    int (*PollJudgeEvent)(JudgeEvent *);
    int (*GetAudioStats)(AudioStats *);
    void (*Shutdown)(void);
};

void usage(void) {
    // Prints the command-line arguments accepted by this practice-mode test.
    std::cout << "usage: PracticeModeSlowDownTest N fs <iDevice> <oDevice> <iChannelOffset> "
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
    // Opens the DLL and binds only the APIs used by this test.
#ifdef _WIN32
    *plugin = {};
    plugin->module = LoadLibraryA(dllPath.c_str());
    if (plugin->module == nullptr)
        return false;

    return loadPluginFunction(plugin, "Initialize", &plugin->Initialize) &&
           loadPluginFunction(plugin, "LoadChart", &plugin->LoadChart) &&
           loadPluginFunction(plugin, "StartSlowPracticeSession", &plugin->StartSlowPracticeSession) &&
           loadPluginFunction(plugin, "SetPracticeSpeed", &plugin->SetPracticeSpeed) &&
           loadPluginFunction(plugin, "StopSession", &plugin->StopSession) &&
           loadPluginFunction(plugin, "PollJudgeEvent", &plugin->PollJudgeEvent) &&
           loadPluginFunction(plugin, "GetAudioStats", &plugin->GetAudioStats) &&
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

const char *judgeResultText(int result) {
    // Converts one native judge result to text for the console.
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
    if (argc > 3) inputDevice = (unsigned int)std::atoi(argv[3]);
    if (argc > 4) outputDevice = (unsigned int)std::atoi(argv[4]);
    if (argc > 5) inputOffset = (unsigned int)std::atoi(argv[5]);
    if (argc > 6) outputOffset = (unsigned int)std::atoi(argv[6]);
    if (argc > 7) chartPath = argv[7];
    if (argc > 8) dllPath = argv[8];

    PluginApi plugin = {};
    if (!loadPlugin(dllPath, &plugin)) {
        std::cout << "Failed to load plugin: " << dllPath << "\n";
        unloadPlugin(&plugin);
        return 1;
    }

    constexpr float speeds[] = {0.25f, 0.50f, 0.75f, 1.00f, 1.25f};
    constexpr auto stepDuration = std::chrono::seconds(3);
    int result = 0;
    int speedIndex = 0;
    std::chrono::steady_clock::time_point stepStartedAt = {};
    if (plugin.Initialize(channels, sampleRate, inputDevice, outputDevice, inputOffset, outputOffset) != 0 ||
        plugin.LoadChart(chartPath.c_str()) != 0) {
        std::cout << "Failed to initialize practice session.\n";
        result = 1;
        goto cleanup;
    }

    plugin.SetPracticeSpeed(speeds[speedIndex]);

    std::cout << "Slow practice: song output disabled\n";
    std::cout << "Speed changes automatically every 3 seconds: 25%, 50%, 75%, 100%, 125%\n";
    if (plugin.StartSlowPracticeSession() != 0) {
        std::cout << "Failed to start slow practice session.\n";
        result = 1;
        goto cleanup;
    }

    stepStartedAt = std::chrono::steady_clock::now();
    while (speedIndex < (int)(sizeof(speeds) / sizeof(speeds[0]))) {
        auto now = std::chrono::steady_clock::now();
        if (now - stepStartedAt >= stepDuration) {
            speedIndex++;
            if (speedIndex == (int)(sizeof(speeds) / sizeof(speeds[0])))
                break;

            plugin.SetPracticeSpeed(speeds[speedIndex]);
            stepStartedAt = now;
            std::cout << "\nSpeed changed to " << (int)(speeds[speedIndex] * 100.0f) << "%\n";
        }

        AudioStats stats = {};
        if (plugin.GetAudioStats(&stats) != 0)
            break;

        std::cout << "\rSpeed " << std::setw(3) << (int)(speeds[speedIndex] * 100.0f) << "%"
                  << " | chart " << std::fixed << std::setprecision(2)
                  << std::setw(7) << stats.chartTimeMs / 1000.0 << " s"
                  << " | next " << stats.nextNoteIndex << "/" << stats.totalNotes << std::flush;

        JudgeEvent event = {};
        while (plugin.PollJudgeEvent(&event) == 1) {
            std::cout << "\nNote " << event.noteIndex + 1 << ": " << judgeResultText(event.result)
                      << " (" << event.noteName << ", " << std::showpos << std::fixed
                      << std::setprecision(1) << event.errorMs << std::noshowpos << " ms)\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\nAutomatic speed demonstration finished.\n";

cleanup:
    plugin.StopSession();
    plugin.Shutdown();
    unloadPlugin(&plugin);
    return result;
}
