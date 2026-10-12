#include "../../src/main.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

struct PluginApi {
#ifdef _WIN32
    HMODULE module;
#endif
    unsigned int (*GetAudioDeviceCount)(void);
    int (*GetAudioDeviceInfo)(unsigned int, AudioDeviceInfo *);
    int (*InitializeWithAudioDevice)(unsigned int,
                                     unsigned int,
                                     unsigned int,
                                     unsigned int,
                                     unsigned int,
                                     unsigned int);
    void (*Shutdown)(void);
};

void usage(void) {
    // Prints the optional channel and sample-rate settings for this connection test.
    std::cout << "usage: driverConnectTest <inputOffset> <outputOffset> <sampleRate> <dllPath>\n";
    std::exit(0);
}

template <typename FunctionType>
bool loadPluginFunction(PluginApi *plugin, const char *name, FunctionType *outFunction) {
    // Loads one exported native plugin function by name.
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
    // Opens the plugin DLL and binds the WASAPI device APIs used by this test.
#ifdef _WIN32
    *plugin = {};
    plugin->module = LoadLibraryA(dllPath.c_str());
    if (plugin->module == nullptr)
        return false;

    return loadPluginFunction(plugin, "GetAudioDeviceCount", &plugin->GetAudioDeviceCount) &&
           loadPluginFunction(plugin, "GetAudioDeviceInfo", &plugin->GetAudioDeviceInfo) &&
           loadPluginFunction(plugin, "InitializeWithAudioDevice", &plugin->InitializeWithAudioDevice) &&
           loadPluginFunction(plugin, "Shutdown", &plugin->Shutdown);
#else
    (void)dllPath;
    (void)plugin;
    return false;
#endif
}

void unloadPlugin(PluginApi *plugin) {
    // Releases the DLL after the test has closed its audio stream.
#ifdef _WIN32
    if (plugin->module != nullptr)
        FreeLibrary(plugin->module);
#else
    (void)plugin;
#endif
}

int main(int argc, char *argv[]) {
    unsigned int inputOffset = 0;
    unsigned int outputOffset = 0;
    unsigned int sampleRate = 48000;
    std::string dllPath = "out/build/ninja-debug/PAKNativePlugin.dll";

    if (argc > 5)
        usage();
    if (argc > 1) inputOffset = (unsigned int)std::atoi(argv[1]);
    if (argc > 2) outputOffset = (unsigned int)std::atoi(argv[2]);
    if (argc > 3) sampleRate = (unsigned int)std::atoi(argv[3]);
    if (argc > 4) dllPath = argv[4];

    PluginApi plugin = {};
    if (!loadPlugin(dllPath, &plugin)) {
        std::cerr << "Failed to load plugin DLL: " << dllPath << "\n";
        return 1;
    }

    AudioDeviceInfo deviceToConnect = {};
    bool foundDeviceToConnect = false;
    unsigned int deviceCount = plugin.GetAudioDeviceCount();
    for (unsigned int index = 0; index < deviceCount; ++index) {
        AudioDeviceInfo device = {};
        if (plugin.GetAudioDeviceInfo(index, &device) == 0) {
            std::cout << "WASAPI device: " << device.name
                      << " (input " << device.inputChannels
                      << ", output " << device.outputChannels << ")\n";
            if (device.inputChannels > inputOffset && device.outputChannels > outputOffset) {
                deviceToConnect = device;
                foundDeviceToConnect = true;
                break;
            }
        }
    }

    if (!foundDeviceToConnect) {
        std::cerr << "No WASAPI device supports the requested input/output channels.\n";
        unloadPlugin(&plugin);
        return 1;
    }

    std::cout << "Connecting WASAPI device " << deviceToConnect.name
              << ": input channel " << inputOffset + 1
              << ", output channel " << outputOffset + 1
              << ", " << sampleRate << " Hz\n";
    int result = plugin.InitializeWithAudioDevice(1,
                                                   sampleRate,
                                                   deviceToConnect.id,
                                                   deviceToConnect.id,
                                                   inputOffset,
                                                   outputOffset);
    if (result != 0) {
        std::cerr << "Connection failed. RtAudio prints the driver error above.\n";
        plugin.Shutdown();
        unloadPlugin(&plugin);
        return 1;
    }

    std::cout << "Connection succeeded.\n";
    plugin.Shutdown();
    unloadPlugin(&plugin);
    return 0;
}
