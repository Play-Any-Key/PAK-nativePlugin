#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

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
    int (*GetJudgeProcessingStats)(JudgeProcessingStats *);
    void (*Shutdown)(void);
};

void usage(void) {
    // Prints the command-line arguments accepted by this processing monitor.
    std::cout << "usage: judgeConsumeTest N fs <iDevice> <oDevice> <iChannelOffset> "
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
    // Opens the DLL and binds only APIs required for processing-rate monitoring.
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
           loadPluginFunction(plugin, "GetJudgeProcessingStats", &plugin->GetJudgeProcessingStats) &&
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
    // Checks whether enter was pressed without blocking the monitor loop.
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

void printStats(const JudgeProcessingStats &previous, const JudgeProcessingStats &current) {
    // Prints one compact one-second callback-to-judge processing report.
    unsigned int queuedPerSecond = current.queuedAudioBlocks - previous.queuedAudioBlocks;
    unsigned int processedPerSecond =
        current.processedAudioBlocks - previous.processedAudioBlocks;
    unsigned int droppedPerSecond = current.droppedAudioBlocks - previous.droppedAudioBlocks;
    unsigned int backlog = current.queuedAudioBlocks - current.processedAudioBlocks;

    std::cout << "Judge | input " << queuedPerSecond << " block/s | processed "
              << processedPerSecond << " block/s | backlog " << backlog << " | dropped "
              << droppedPerSecond << "\n" << std::flush;
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

    int result = 0;
    if (plugin.Initialize(channels, sampleRate, inputDevice, outputDevice, inputOffset, outputOffset) != 0 ||
        plugin.LoadChart(chartPath.c_str()) != 0 ||
        plugin.StartFingeringPracticeSession() != 0) {
        std::cout << "Failed to start judge processing monitor.\n";
        result = 1;
        goto cleanup;
    }

    std::cout << "Monitoring judge processing. Press enter to quit.\n";
    JudgeProcessingStats previous = {};
    if (plugin.GetJudgeProcessingStats(&previous) != 0) {
        result = 1;
        goto cleanup;
    }

    auto lastReportAt = std::chrono::steady_clock::now();
    while (!enterPressed()) {
        auto now = std::chrono::steady_clock::now();
        if (now - lastReportAt >= std::chrono::seconds(1)) {
            JudgeProcessingStats current = {};
            if (plugin.GetJudgeProcessingStats(&current) != 0) {
                result = 1;
                break;
            }

            printStats(previous, current);
            previous = current;
            lastReportAt = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

cleanup:
    plugin.StopSession();
    plugin.Shutdown();
    unloadPlugin(&plugin);
    return result;
}
