#include "../../src/main.h"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <conio.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

struct PluginApi {
#ifdef _WIN32
    HMODULE module;
#endif
    int (*InitializeWithAudioDevice)(
        unsigned int, unsigned int, unsigned int, unsigned int, unsigned int, unsigned int);
    int (*InitializeAudioTest)(void);
    int (*StartAudioTest)(void);
    void (*StopAudioTest)(void);
    int (*GetAudioTestOutputLevelDb)(float *);
    void (*Shutdown)(void);
};

void usage(void) {
    // Prints the optional instrument input and output settings accepted by this test.
    std::cout << "usage: audioTestApiTest <channels> <sampleRate> <inputDeviceId> <outputDeviceId> "
                 "<inputOffset> <outputOffset> <dllPath>\n";
    std::exit(0);
}

int main(int argc, char *argv[]) {
    unsigned int channels = 2;
    unsigned int sampleRate = 48000;
    unsigned int inputDeviceId = 0;
    unsigned int outputDeviceId = 0;
    unsigned int inputOffset = 0;
    unsigned int outputOffset = 0;
    std::string dllPath = "out/build/ninja-debug/PAKNativePlugin.dll";

    if (argc > 8)
        usage();
    if (argc > 1) channels = (unsigned int)std::atoi(argv[1]);
    if (argc > 2) sampleRate = (unsigned int)std::atoi(argv[2]);
    if (argc > 3) inputDeviceId = (unsigned int)std::atoi(argv[3]);
    if (argc > 4) outputDeviceId = (unsigned int)std::atoi(argv[4]);
    if (argc > 5) inputOffset = (unsigned int)std::atoi(argv[5]);
    if (argc > 6) outputOffset = (unsigned int)std::atoi(argv[6]);
    if (argc > 7) dllPath = argv[7];

#ifdef _WIN32
    PluginApi plugin = {};
    plugin.module = LoadLibraryA(dllPath.c_str());
    if (plugin.module == nullptr) {
        std::cerr << "Failed to load plugin DLL: " << dllPath << "\n";
        return 1;
    }

    plugin.InitializeAudioTest = reinterpret_cast<decltype(plugin.InitializeAudioTest)>(
        GetProcAddress(plugin.module, "InitializeAudioTest"));
    plugin.InitializeWithAudioDevice = reinterpret_cast<decltype(plugin.InitializeWithAudioDevice)>(
        GetProcAddress(plugin.module, "InitializeWithAudioDevice"));
    plugin.StartAudioTest = reinterpret_cast<decltype(plugin.StartAudioTest)>(
        GetProcAddress(plugin.module, "StartAudioTest"));
    plugin.StopAudioTest = reinterpret_cast<decltype(plugin.StopAudioTest)>(
        GetProcAddress(plugin.module, "StopAudioTest"));
    plugin.GetAudioTestOutputLevelDb = reinterpret_cast<decltype(plugin.GetAudioTestOutputLevelDb)>(
        GetProcAddress(plugin.module, "GetAudioTestOutputLevelDb"));
    plugin.Shutdown = reinterpret_cast<decltype(plugin.Shutdown)>(
        GetProcAddress(plugin.module, "Shutdown"));
    if (plugin.InitializeWithAudioDevice == nullptr || plugin.InitializeAudioTest == nullptr ||
        plugin.StartAudioTest == nullptr ||
        plugin.StopAudioTest == nullptr || plugin.GetAudioTestOutputLevelDb == nullptr ||
        plugin.Shutdown == nullptr) {
        std::cerr << "Failed to load output-test APIs.\n";
        FreeLibrary(plugin.module);
        return 1;
    }

    if (plugin.InitializeWithAudioDevice(channels,
                                         sampleRate,
                                         inputDeviceId,
                                         outputDeviceId,
                                         inputOffset,
                                         outputOffset) != 0) {
        std::cerr << "Failed to initialize the pass-through stream.\n";
        plugin.Shutdown();
        FreeLibrary(plugin.module);
        return 1;
    }
    if (plugin.InitializeAudioTest() != 0) {
        std::cerr << "Failed to prepare the pass-through test.\n";
        plugin.Shutdown();
        FreeLibrary(plugin.module);
        return 1;
    }
    if (plugin.StartAudioTest() != 0) {
        std::cerr << "Failed to start the pass-through test.\n";
        plugin.Shutdown();
        FreeLibrary(plugin.module);
        return 1;
    }

    std::cout << "Pass-through started. Play the connected guitar; press any key to stop.\n";
    for (;;) {
        float levelDb = 0.0f;
        if (plugin.GetAudioTestOutputLevelDb(&levelDb) != 0) {
            std::cerr << "Failed to read the pass-through output level.\n";
            plugin.StopAudioTest();
            plugin.Shutdown();
            FreeLibrary(plugin.module);
            return 1;
        }
        std::cout << "\rOutput level: " << std::fixed << std::setprecision(2) << levelDb
                  << " dBFS    " << std::flush;

        if (_kbhit()) {
            _getch();
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    plugin.StopAudioTest();
    plugin.Shutdown();
    FreeLibrary(plugin.module);
    return 0;
#else
    (void)channels;
    (void)sampleRate;
    (void)inputDeviceId;
    (void)outputDeviceId;
    (void)inputOffset;
    (void)outputOffset;
    (void)dllPath;
    std::cerr << "audioTestApiTest requires Windows.\n";
    return 1;
#endif
}
