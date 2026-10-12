#include <cstring>
#include <cstdint>
#include <atomic>
#include <iostream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "asio.h"
#include "asiodrivers.h"

extern AsioDrivers *asioDrivers;

ASIOBufferInfo *g_buffers = nullptr;
long g_bufferFrames = 0;
std::atomic<unsigned int> g_callbackCount = 0;
bool g_postOutput = false;
std::atomic<bool> g_resetRequested = false;

HWND createAsioHostWindow(void) {
    // Creates the application-owned window required as the ASIO host reference.
    const char *className = "PAKAsioDirectConnectTest";
    WNDCLASSA windowClass = {};
    windowClass.lpfnWndProc = DefWindowProcA;
    windowClass.hInstance = GetModuleHandleA(nullptr);
    windowClass.lpszClassName = className;
    RegisterClassA(&windowClass);
    return CreateWindowExA(0,
                           className,
                           "PAK ASIO Test",
                           WS_OVERLAPPEDWINDOW,
                           0,
                           0,
                           1,
                           1,
                           nullptr,
                           nullptr,
                           windowClass.hInstance,
                           nullptr);
}

void processBuffer(long index) {
    // Copies the selected ASIO driver's input channel 1 to output channel 1 for one buffer.
    std::memcpy(g_buffers[1].buffers[index],
                g_buffers[0].buffers[index],
                (size_t)g_bufferFrames * sizeof(int32_t));
    g_callbackCount.fetch_add(1);
    if (g_postOutput)
        ASIOOutputReady();
}

ASIOTime *bufferSwitchTimeInfo(ASIOTime *, long index, ASIOBool) {
    // Processes an ASIO 2 time-info callback.
    processBuffer(index);
    return nullptr;
}

void bufferSwitch(long index, ASIOBool processNow) {
    // Converts a legacy ASIO callback into the ASIO 2 time-info callback flow.
    ASIOTime timeInfo = {};
    if (ASIOGetSamplePosition(&timeInfo.timeInfo.samplePosition,
                              &timeInfo.timeInfo.systemTime) == ASE_OK)
        timeInfo.timeInfo.flags = kSystemTimeValid | kSamplePositionValid;
    bufferSwitchTimeInfo(&timeInfo, index, processNow);
}

void sampleRateChanged(ASIOSampleRate) {
    // Accepts a driver sample-rate notification during buffer setup.
}

long asioMessage(long selector, long value, void *, double *) {
    // Handles the ASIO host messages required by the SDK sample lifecycle.
    if (selector == kAsioSelectorSupported) {
        if (value == kAsioResetRequest ||
            value == kAsioResyncRequest ||
            value == kAsioLatenciesChanged ||
            value == kAsioEngineVersion ||
            value == kAsioSupportsTimeInfo ||
            value == kAsioSupportsTimeCode)
            return 1;
        return 0;
    }
    if (selector == kAsioResetRequest) {
        g_resetRequested.store(true);
        return 1;
    }
    if (selector == kAsioResyncRequest || selector == kAsioLatenciesChanged)
        return 1;
    if (selector == kAsioEngineVersion)
        return 2;
    if (selector == kAsioSupportsTimeInfo)
        return 1;
    return 0;
}

ASIOCallbacks g_callbacks = {
    bufferSwitch,
    sampleRateChanged,
    asioMessage,
    bufferSwitchTimeInfo};

int main(int argc, char *argv[]) {
    // Lists registered ASIO drivers, then initializes the driver chosen by its list number.
    (void)argc;
    (void)argv;

    HWND hostWindow = createAsioHostWindow();
    if (hostWindow == nullptr) {
        std::cerr << "Failed to create the ASIO host window.\n";
        return 1;
    }

    asioDrivers = new AsioDrivers();
    std::cerr << "Enumerating registered ASIO drivers...\n";
    std::vector<std::string> driverNames;
    for (long index = 0; index < asioDrivers->asioGetNumDev(); ++index) {
        char name[128] = {};
        if (asioDrivers->asioGetDriverName((int)index, name, sizeof(name)) != 0)
            continue;

        driverNames.push_back(name);
        std::cout << driverNames.size() - 1 << ". " << name << "\n";
    }

    std::cout << "Select ASIO driver number: ";
    unsigned int driverIndex = 0;
    std::cin >> driverIndex;
    std::string targetName = driverNames[driverIndex];
    std::cerr << "Connecting ASIO driver: " << targetName << "\n";

    if (!asioDrivers->loadDriver(const_cast<char *>(targetName.c_str()))) {
        std::cerr << "Failed to load ASIO driver: " << targetName << "\n";
        delete asioDrivers;
        asioDrivers = nullptr;
        DestroyWindow(hostWindow);
        return 1;
    }

    ASIODriverInfo info = {};
    info.asioVersion = 2;
    info.sysRef = hostWindow;

    ASIOError result = ASIOInit(&info);
    if (result != ASE_OK) {
        std::cerr << "ASIOInit failed for " << targetName << ": " << result;
        if (info.errorMessage[0] != '\0')
            std::cerr << " (" << info.errorMessage << ")";
        std::cerr << "\n";
        asioDrivers->removeCurrentDriver();
        delete asioDrivers;
        asioDrivers = nullptr;
        DestroyWindow(hostWindow);
        return 1;
    }

    long inputChannels = 0;
    long outputChannels = 0;
    result = ASIOGetChannels(&inputChannels, &outputChannels);
    if (result != ASE_OK) {
        std::cerr << "ASIOGetChannels failed: " << result << "\n";
        ASIOExit();
        delete asioDrivers;
        asioDrivers = nullptr;
        DestroyWindow(hostWindow);
        return 1;
    }

    std::cerr << "Connected: " << info.name
              << " (input " << inputChannels
              << ", output " << outputChannels << ")\n";

    long minimumBufferSize = 0;
    long maximumBufferSize = 0;
    long preferredBufferSize = 0;
    long bufferGranularity = 0;
    result = ASIOGetBufferSize(&minimumBufferSize,
                               &maximumBufferSize,
                               &preferredBufferSize,
                               &bufferGranularity);
    if (result != ASE_OK) {
        std::cerr << "ASIOGetBufferSize failed: " << result << "\n";
        ASIOExit();
        delete asioDrivers;
        asioDrivers = nullptr;
        DestroyWindow(hostWindow);
        return 1;
    }

    std::cerr << "Buffer size: min " << minimumBufferSize
              << ", max " << maximumBufferSize
              << ", preferred " << preferredBufferSize
              << ", granularity " << bufferGranularity << "\n";

    ASIOBufferInfo buffers[2] = {};
    buffers[0].isInput = ASIOTrue;
    buffers[0].channelNum = 0;
    buffers[1].isInput = ASIOFalse;
    buffers[1].channelNum = 0;
    ASIOSampleRate currentSampleRate = {};
    result = ASIOGetSampleRate(&currentSampleRate);
    if (result != ASE_OK) {
        std::cerr << "ASIOGetSampleRate failed: " << result << "\n";
        ASIOExit();
        delete asioDrivers;
        asioDrivers = nullptr;
        DestroyWindow(hostWindow);
        return 1;
    }

    g_postOutput = ASIOOutputReady() == ASE_OK;
    std::cerr << "Sample-rate query succeeded, ASIOOutputReady: "
              << (g_postOutput ? "supported" : "not supported") << "\n";
    result = ASIOCreateBuffers(buffers, 2, preferredBufferSize, &g_callbacks);
    if (result != ASE_OK) {
        std::cerr << "ASIOCreateBuffers failed: " << result << "\n";
        ASIOExit();
        delete asioDrivers;
        asioDrivers = nullptr;
        DestroyWindow(hostWindow);
        return 1;
    }

    ASIOChannelInfo inputInfo = {};
    inputInfo.isInput = ASIOTrue;
    inputInfo.channel = 0;
    ASIOChannelInfo outputInfo = {};
    outputInfo.isInput = ASIOFalse;
    outputInfo.channel = 0;
    result = ASIOGetChannelInfo(&inputInfo);
    if (result == ASE_OK)
        result = ASIOGetChannelInfo(&outputInfo);
    if (result != ASE_OK) {
        std::cerr << "ASIOGetChannelInfo failed: " << result << "\n";
        ASIODisposeBuffers();
        ASIOExit();
        delete asioDrivers;
        asioDrivers = nullptr;
        DestroyWindow(hostWindow);
        return 1;
    }

    std::cerr << "Created input/output buffers. Sample types: input " << inputInfo.type
              << ", output " << outputInfo.type << "\n";
    if (inputInfo.type != ASIOSTInt32LSB || outputInfo.type != ASIOSTInt32LSB) {
        std::cerr << "This loopback test requires 32-bit PCM input and output.\n";
        ASIODisposeBuffers();
        ASIOExit();
        delete asioDrivers;
        asioDrivers = nullptr;
        DestroyWindow(hostWindow);
        return 1;
    }

    g_buffers = buffers;
    g_bufferFrames = preferredBufferSize;
    result = ASIOStart();
    if (result != ASE_OK) {
        std::cerr << "ASIOStart failed: " << result << "\n";
        ASIODisposeBuffers();
        ASIOExit();
        delete asioDrivers;
        asioDrivers = nullptr;
        DestroyWindow(hostWindow);
        return 1;
    }

    std::cerr << "Running input-to-output loopback for 2 seconds...\n";
    ULONGLONG endTime = GetTickCount64() + 2000;
    while (GetTickCount64() < endTime) {
        MSG message;
        while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        Sleep(10);
    }
    result = ASIOStop();
    if (result != ASE_OK)
        std::cerr << "ASIOStop failed: " << result << "\n";
    else
        std::cerr << "Stopped after " << g_callbackCount.load() << " callbacks.\n";
    if (g_resetRequested.load())
        std::cerr << "Driver requested an ASIO reset during streaming.\n";

    ASIODisposeBuffers();
    std::cerr << "Closing ASIO driver...\n";
    ASIOExit();
    std::cerr << "Releasing ASIO driver...\n";
    delete asioDrivers;
    asioDrivers = nullptr;
    std::cerr << "ASIO direct test completed.\n";
    DestroyWindow(hostWindow);
    return 0;
}
