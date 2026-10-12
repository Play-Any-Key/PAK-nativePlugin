#include "../../src/main.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

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
    int (*Initialize)(unsigned int,
                      unsigned int,
                      unsigned int,
                      unsigned int,
                      unsigned int,
                      unsigned int);
    int (*StartSession)(void);
    void (*StopSession)(void);
    void (*SetDSPParams)(float, float, float);
    int (*PollGuitarInputEvent)(GuitarInputEvent *);
    int (*GetAudioStats)(AudioStats *);
    void (*Shutdown)(void);
};

std::string midiToNoteName(int midi) {
    // Converts a MIDI note number to a note name for console display.
    if (midi <= 0)
        return "--";

    const char *noteNames[] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"};
    int octave = midi / 12 - 1;
    return std::string(noteNames[midi % 12]) + std::to_string(octave);
}

void usage(void) {
    // Command-line usage for the DLL-backed guitar input test.
    std::cout << "\nusage: guitarInputTest [N] [fs] <iDevice> <oDevice> <iChannelOffset> "
                 "<oChannelOffset> <dllPath>\n";
    std::cout << "    default: N = 2, fs = 48000, PAKNativePlugin.dll in the executable folder\n";
    std::cout << "    where N = number of channels,\n";
    std::cout << "    fs = the sample rate,\n";
    std::cout << "    iDevice = optional input device index to use (default = 0),\n";
    std::cout << "    oDevice = optional output device index to use (default = 0),\n";
    std::cout << "    iChannelOffset = an optional input channel offset (default = 0),\n";
    std::cout << "    oChannelOffset = optional output channel offset (default = 0),\n";
    std::cout << "    and dllPath = optional PAKNativePlugin.dll path.\n\n";
}

bool enterPressed(void) {
    // Checks whether the user pressed enter without blocking the polling loop.
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
    // Loads PAKNativePlugin.dll and binds the guitar input API entry points.
#ifdef _WIN32
    *plugin = {};
    plugin->module = LoadLibraryA(dllPath.c_str());
    if (plugin->module == nullptr)
        return false;

    if (!loadPluginFunction(plugin, "Initialize", &plugin->Initialize))
        return false;
    if (!loadPluginFunction(plugin, "StartSession", &plugin->StartSession))
        return false;
    if (!loadPluginFunction(plugin, "StopSession", &plugin->StopSession))
        return false;
    if (!loadPluginFunction(plugin, "SetDSPParams", &plugin->SetDSPParams))
        return false;
    if (!loadPluginFunction(plugin, "PollGuitarInputEvent", &plugin->PollGuitarInputEvent))
        return false;
    if (!loadPluginFunction(plugin, "GetAudioStats", &plugin->GetAudioStats))
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

void printGuitarInputEvent(const GuitarInputEvent &event) {
    // Prints one guitar input event as MIDI and note name.
    std::cout << "\ninput "
              << midiToNoteName(event.midi)
              << " (MIDI " << event.midi << ")"
              << " @ " << event.audioTimeMs << " ms"
              << "\n"
              << std::flush;
}

int main(int argc, char *argv[]) {
    unsigned int channels = 2, fs = 48000, oDevice = 0, iDevice = 0, iOffset = 0, oOffset = 0;
    std::string dllPath = "PAKNativePlugin.dll";

    // Uses default stereo 48 kHz settings when launched without command-line arguments.
    if (argc > 8) {
        usage();
        return 1;
    }

    if (argc > 1)
        channels = (unsigned int)atoi(argv[1]);
    if (argc > 2)
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
        dllPath = argv[7];

    PluginApi plugin = {};
    if (!loadPlugin(dllPath, &plugin)) {
        std::cout << "Failed to load plugin: " << dllPath << std::endl;
        unloadPlugin(&plugin);
        return 1;
    }

    int result = 0;
    if (plugin.Initialize(channels, fs, iDevice, oDevice, iOffset, oOffset) != 0) {
        std::cout << "Plugin Initialize failed." << std::endl;
        result = 1;
        goto cleanup;
    }

    plugin.SetDSPParams(4.0f, 0.5f, 0.2f);

    std::cout << "\nLoaded plugin DLL : " << dllPath << "\n";
    std::cout << "Running guitar input test ... press <enter> to quit.\n";

    if (plugin.StartSession() != 0) {
        std::cout << "Plugin StartSession failed." << std::endl;
        result = 1;
        goto cleanup;
    }

    unsigned int inputEventCount = 0;
    while (!enterPressed()) {
        AudioStats stats = {};
        if (plugin.GetAudioStats(&stats) != 0)
            break;

        GuitarInputEvent event = {};
        while (plugin.PollGuitarInputEvent(&event)) {
            inputEventCount++;
            printGuitarInputEvent(event);
        }

        std::cout << "\rtime " << stats.audioTimeMs << " ms"
                  << " | events " << inputEventCount
                  << " | dropped audio " << stats.droppedAudioBlocks
                  << "        " << std::flush;
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
