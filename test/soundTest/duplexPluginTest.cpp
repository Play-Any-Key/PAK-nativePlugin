#include <cstdlib>
#include <iostream>
#include <windows.h>

using DuplexPluginInitFn = int (*)(unsigned int,
                                   unsigned int,
                                   unsigned int,
                                   unsigned int,
                                   unsigned int,
                                   unsigned int);
using DuplexPluginStartFn = int (*)();
using DuplexPluginStopFn = void (*)();
using DuplexPluginShutdownFn = void (*)();

int main(int argc, char* argv[]) {
    const char* dllPath = (argc > 1) ? argv[1] : "duplexPlugin.dll";
    const unsigned int channels = (argc > 2) ? static_cast<unsigned int>(std::strtoul(argv[2], nullptr, 10)) : 2;
    const unsigned int sampleRate = (argc > 3) ? static_cast<unsigned int>(std::strtoul(argv[3], nullptr, 10)) : 48000;
    const unsigned int inputDevice = (argc > 4) ? static_cast<unsigned int>(std::strtoul(argv[4], nullptr, 10)) : 0;
    const unsigned int outputDevice = (argc > 5) ? static_cast<unsigned int>(std::strtoul(argv[5], nullptr, 10)) : 0;
    const unsigned int inputOffset = (argc > 6) ? static_cast<unsigned int>(std::strtoul(argv[6], nullptr, 10)) : 0;
    const unsigned int outputOffset = (argc > 7) ? static_cast<unsigned int>(std::strtoul(argv[7], nullptr, 10)) : 0;

    // Design intent: validate DLL export calls without Unity runtime.
    HMODULE dll = LoadLibraryA(dllPath);
    if (dll == nullptr) {
        std::cerr << "LoadLibraryA failed. path=" << dllPath << ", error=" << GetLastError() << '\n';
        return 1;
    }

    auto init = reinterpret_cast<DuplexPluginInitFn>(GetProcAddress(dll, "DuplexPlugin_Init"));
    auto start = reinterpret_cast<DuplexPluginStartFn>(GetProcAddress(dll, "DuplexPlugin_Start"));
    auto stop = reinterpret_cast<DuplexPluginStopFn>(GetProcAddress(dll, "DuplexPlugin_Stop"));
    auto shutdown = reinterpret_cast<DuplexPluginShutdownFn>(GetProcAddress(dll, "DuplexPlugin_Shutdown"));
    if (init == nullptr || start == nullptr || stop == nullptr || shutdown == nullptr) {
        std::cerr << "GetProcAddress failed. error=" << GetLastError() << '\n';
        FreeLibrary(dll);
        return 1;
    }

    const int initResult = init(channels, sampleRate, inputDevice, outputDevice, inputOffset, outputOffset);
    if (initResult != 0) {
        std::cerr << "DuplexPlugin_Init failed. result=" << initResult << '\n';
        shutdown();
        FreeLibrary(dll);
        return 1;
    }

    const int startResult = start();
    if (startResult != 0) {
        std::cerr << "DuplexPlugin_Start failed. result=" << startResult << '\n';
        shutdown();
        FreeLibrary(dll);
        return 1;
    }

    // Design intent: keep stream alive until user finishes microphone loopback check.
    std::cout << "Running duplex stream... press Enter to stop.\n";
    std::cin.get();

    stop();
    shutdown();
    FreeLibrary(dll);

    std::cout << "duplexPlugin test finished.\n";
    return 0;
}
