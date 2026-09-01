#include "setting.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "RtAudio.h"
#include "asio.h"
#include "asiodrivers.h"
#include "audioQueue.h"
#include "eventQueue.h"
#include "song.h"

#define FORMAT RTAUDIO_SINT16

extern AsioDrivers *asioDrivers;
static bool g_isAsioDriverSelected = false;

int inoutRhythmGame(void *outputBuffer,
                    void *inputBuffer,
                    unsigned int nBufferFrames,
                    double streamTime,
                    RtAudioStreamStatus status,
                    void *data);

int inoutAudioTest(void *outputBuffer,
                   void *inputBuffer,
                   unsigned int nBufferFrames,
                   double streamTime,
                   RtAudioStreamStatus status,
                   void *data);

static int initializeAudioDriver(RtAudio::Api api,
                                 unsigned int channels,
                                 unsigned int sampleRate,
                                 unsigned int inputDeviceId,
                                 unsigned int outputDeviceId,
                                 unsigned int inputOffset,
                                 unsigned int outputOffset) {
    // Initializes the selected driver, device, and input/output channel offsets.
    Shutdown();

    g_adac = new RtAudio(api);
    g_adac->showWarnings(true);

    g_state.chart = {};
    g_state.channels = channels;
    g_state.sampleRate = sampleRate;
    g_state.bufferFrames = 128;
    g_state.nextNoteIndex.store(0);
    g_state.lastDetectedMidi = -1;
    g_state.input = nullptr;
    g_state.pitch = nullptr;
    g_state.onset = nullptr;
    g_state.chordInput = nullptr;
    g_state.chordSpectrum = nullptr;
    g_state.pitchDetector = nullptr;
    g_state.onsetDetector = nullptr;
    g_state.chordFft = nullptr;
    g_state.inputGain.store(4.0f);
    g_state.outputGain.store(0.5f);
    g_state.songVolume.store(1.0f);
    g_state.lpfAlpha.store(0.2f);
    // Loads the selected NAM amp model before the audio callback starts.
    try {
        g_state.namModel = nam::get_dsp(
            std::filesystem::path(g_state.namModelPath));
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Failed to load NAM model: %s\n", error.what());
        Shutdown();
        return -1;
    } catch (...) {
        std::fprintf(stderr, "Failed to load NAM model.\n");
        Shutdown();
        return -1;
    }
    g_state.namInput.assign(g_state.bufferFrames, 0.0);
    g_state.namOutput.assign(g_state.bufferFrames, 0.0);
    g_state.audioTestMode = false;
    g_state.stopRequested.store(true);
    g_state.requestedSessionMode.store(SessionMode_GuitarInput);
    g_state.sessionMode.store(SessionMode_None);
    g_state.detectedOnsets.store(0);
    g_state.startedFingeringJudgments.store(0);
    g_state.passedChordJudgments.store(0);
    g_state.failedChordJudgments.store(0);
    g_state.guitarInputIntervalMs.store(150.0);
    g_state.lastGuitarInputEventAudioTimeMs.store(-150.0);
    g_state.sessionStreamTimeOffset.store(0.0);
    g_state.sessionClockStarted.store(false);
    g_state.lastChartTimeMs.store(-COUNTDOWN_MS);
    g_state.practiceSpeed.store(1.0f);
    g_state.lpfState.assign(channels, 0.0f);
    g_state.songSamples.clear();
    g_state.songChannels = 0;
    g_state.songFrames = 0;
    g_state.metronomeSamples.clear();
    g_state.metronomeChannels = 0;
    g_state.metronomeFrames = 0;
    g_state.pitchObservations.clear();
    g_state.pendingGuitarInputs.clear();
    g_state.pendingJudgments.clear();

    RtAudio::StreamParameters iParams, oParams;
    iParams.nChannels = channels;
    iParams.firstChannel = inputOffset;
    oParams.nChannels = channels;
    oParams.firstChannel = outputOffset;
    iParams.deviceId = inputDeviceId == 0 ? g_adac->getDefaultInputDevice() : inputDeviceId;
    oParams.deviceId = outputDeviceId == 0 ? g_adac->getDefaultOutputDevice() : outputDeviceId;

    RtAudio::StreamOptions options;
    if (g_adac->openStream(&oParams,
                           &iParams,
                           FORMAT,
                           sampleRate,
                           &g_state.bufferFrames,
                           &inoutRhythmGame,
                           (void *)&g_state,
                           &options)) {
        Shutdown();
        return -1;
    }

    g_state.fingeringPcmRing.assign(FINGERING_PCM_RING_FRAMES, 0.0f);
    prepareAudioQueue(&g_state.audioQueue, 64, g_state.bufferFrames * channels);
    preparePluginEventQueue(&g_state.eventQueue, 64);

    g_state.input = new_fvec(g_state.bufferFrames);
    g_state.pitch = new_fvec(1);
    g_state.onset = new_fvec(1);
    g_state.chordInput = new_fvec(CHORD_FFT_SIZE);
    g_state.chordSpectrum = new_cvec(CHORD_FFT_SIZE);
    g_state.pitchDetector = new_aubio_pitch("default", 2048, g_state.bufferFrames, sampleRate);
    g_state.onsetDetector = new_aubio_onset("default", 1024, g_state.bufferFrames, sampleRate);
    g_state.chordFft = new_aubio_fft(CHORD_FFT_SIZE);
    aubio_pitch_set_unit(g_state.pitchDetector, "midi");
    aubio_onset_set_threshold(g_state.onsetDetector, 0.3f);
    if (!initializeSongDecoder()) {
        Shutdown();
        return -1;
    }
    return 0;
}

extern "C" PLUGIN_API int InitializeAudioTest(unsigned int channels,
                                                unsigned int sampleRate,
                                                unsigned int inputDeviceId,
                                                unsigned int outputDeviceId,
                                                unsigned int inputOffset,
                                                unsigned int outputOffset) {
    // Opens a WASAPI pass-through stream without allocating session processing resources.
    Shutdown();

    g_adac = new RtAudio(RtAudio::WINDOWS_WASAPI);
    g_adac->showWarnings(true);
    g_state.channels = channels;
    g_state.sampleRate = sampleRate;
    g_state.bufferFrames = 128;
    g_state.audioTestMode = true;
    g_state.audioTestOutputLevelDb.store(-96.0f);

    RtAudio::StreamParameters iParams, oParams;
    iParams.deviceId = inputDeviceId == 0 ? g_adac->getDefaultInputDevice() : inputDeviceId;
    iParams.nChannels = channels;
    iParams.firstChannel = inputOffset;
    oParams.deviceId = outputDeviceId == 0 ? g_adac->getDefaultOutputDevice() : outputDeviceId;
    oParams.nChannels = channels;
    oParams.firstChannel = outputOffset;
    RtAudio::StreamOptions options;
    if (g_adac->openStream(&oParams,
                           &iParams,
                           FORMAT,
                           sampleRate,
                           &g_state.bufferFrames,
                           &inoutAudioTest,
                           (void *)&g_state,
                           &options)) {
        Shutdown();
        return -1;
    }
    return 0;
}

extern "C" PLUGIN_API int Initialize(unsigned int channels,
                                      unsigned int sampleRate,
                                      unsigned int inputDevice,
                                      unsigned int outputDevice,
                                      unsigned int inputOffset,
                                      unsigned int outputOffset) {
    // Initializes through Windows WASAPI so Windows default devices are used.
    RtAudio audio(RtAudio::WINDOWS_WASAPI);
    std::vector<unsigned int> deviceIds = audio.getDeviceIds();
    unsigned int inputDeviceId = inputDevice == 0 ? 0 : deviceIds[inputDevice];
    unsigned int outputDeviceId = outputDevice == 0 ? 0 : deviceIds[outputDevice];
    return initializeAudioDriver(RtAudio::WINDOWS_WASAPI,
                                 channels,
                                 sampleRate,
                                 inputDeviceId,
                                 outputDeviceId,
                                 inputOffset,
                                 outputOffset);
}

extern "C" PLUGIN_API int InitializeFingeringTest(unsigned int channels,
                                                    unsigned int sampleRate,
                                                    const char *onsetMethod,
                                                    float onsetThreshold) {
    // Prepares a selected onset detector and threshold without opening an audio device.
    Shutdown();

    g_state.chart = {};
    g_state.channels = channels;
    g_state.sampleRate = sampleRate;
    g_state.bufferFrames = 128;
    g_state.inputGain.store(4.0f);
    g_state.outputGain.store(0.5f);
    g_state.songVolume.store(1.0f);
    g_state.lpfAlpha.store(0.2f);
    g_state.namModel.reset();
    g_state.namInput.clear();
    g_state.namOutput.clear();
    g_state.audioTestMode = false;
    g_state.stopRequested.store(true);
    g_state.requestedSessionMode.store(SessionMode_GuitarInput);
    g_state.sessionMode.store(SessionMode_None);
    g_state.guitarInputIntervalMs.store(150.0);
    g_state.practiceSpeed.store(1.0f);
    g_state.lpfState.assign(channels, 0.0f);
    g_state.songSamples.clear();
    g_state.metronomeSamples.clear();
    g_state.pitchObservations.clear();
    g_state.pendingGuitarInputs.clear();
    g_state.pendingJudgments.clear();

    g_state.fingeringPcmRing.assign(FINGERING_PCM_RING_FRAMES, 0.0f);
    prepareAudioQueue(&g_state.audioQueue, 64, g_state.bufferFrames * channels);
    preparePluginEventQueue(&g_state.eventQueue, 64);
    g_state.input = new_fvec(g_state.bufferFrames);
    g_state.pitch = new_fvec(1);
    g_state.onset = new_fvec(1);
    g_state.chordInput = new_fvec(CHORD_FFT_SIZE);
    g_state.chordSpectrum = new_cvec(CHORD_FFT_SIZE);
    g_state.pitchDetector = new_aubio_pitch("default", 2048, g_state.bufferFrames, sampleRate);
    g_state.onsetDetector =
        new_aubio_onset(onsetMethod, 1024, g_state.bufferFrames, sampleRate);
    g_state.chordFft = new_aubio_fft(CHORD_FFT_SIZE);
    aubio_pitch_set_unit(g_state.pitchDetector, "midi");
    aubio_onset_set_threshold(g_state.onsetDetector, onsetThreshold);
    return initializeSongDecoder() ? 0 : -1;
}

extern "C" PLUGIN_API unsigned int GetAsioDriverCount(void) {
    // Returns registered ASIO driver names without loading or initializing a driver.
#ifdef _WIN32
    HKEY asioKey = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO", 0, KEY_READ, &asioKey) != ERROR_SUCCESS)
        return 0;

    unsigned int count = 0;
    char name[256] = {};
    DWORD nameLength = sizeof(name);
    while (RegEnumKeyExA(asioKey, count, name, &nameLength, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        ++count;
        nameLength = sizeof(name);
    }
    RegCloseKey(asioKey);
    return count;
#else
    return 0;
#endif
}

extern "C" PLUGIN_API int GetAsioDriverInfo(unsigned int driverIndex,
                                              AsioDriverInfo *outInfo) {
    // Copies one registered ASIO driver name without loading or initializing it.
#ifdef _WIN32
    HKEY asioKey = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO", 0, KEY_READ, &asioKey) != ERROR_SUCCESS)
        return -1;

    *outInfo = {};
    DWORD nameLength = sizeof(outInfo->name);
    LONG result = RegEnumKeyExA(asioKey,
                                driverIndex,
                                outInfo->name,
                                &nameLength,
                                nullptr,
                                nullptr,
                                nullptr,
                                nullptr);
    RegCloseKey(asioKey);
    return result == ERROR_SUCCESS ? 0 : -1;
#else
    (void)driverIndex;
    (void)outInfo;
    return -1;
#endif
}

extern "C" PLUGIN_API int SelectAsioDriver(const char *driverName,
                                             AudioDeviceInfo *outInfo) {
    // Opens the selected ASIO driver and returns its available channel counts.
#ifdef _WIN32
    releaseSelectedAsioDriver();
    asioDrivers = new AsioDrivers();
    if (!asioDrivers->loadDriver(const_cast<char *>(driverName))) {
        delete asioDrivers;
        asioDrivers = nullptr;
        return -1;
    }

    ASIODriverInfo info = {};
    info.asioVersion = 2;
    info.sysRef = GetDesktopWindow();
    if (ASIOInit(&info) != ASE_OK) {
        releaseSelectedAsioDriver();
        return -1;
    }

    long inputChannels = 0;
    long outputChannels = 0;
    if (ASIOGetChannels(&inputChannels, &outputChannels) != ASE_OK) {
        ASIOExit();
        delete asioDrivers;
        asioDrivers = nullptr;
        return -1;
    }

    *outInfo = {};
    std::strncpy(outInfo->name, info.name, sizeof(outInfo->name) - 1);
    outInfo->inputChannels = (unsigned int)inputChannels;
    outInfo->outputChannels = (unsigned int)outputChannels;
    outInfo->duplexChannels = (unsigned int)(inputChannels < outputChannels
                                                  ? inputChannels
                                                  : outputChannels);
    g_isAsioDriverSelected = true;
    return 0;
#else
    (void)driverName;
    (void)outInfo;
    return -1;
#endif
}

extern "C" PLUGIN_API int InitializeWithAsioDriver(const char *driverName,
                                                      unsigned int channels,
                                                      unsigned int sampleRate,
                                                      unsigned int inputOffset,
                                                      unsigned int outputOffset) {
    // Initializes a duplex stream through the ASIO driver selected by its registered name.
#ifdef _WIN32
    releaseSelectedAsioDriver();

    RtAudio audio(RtAudio::WINDOWS_ASIO);
    std::vector<unsigned int> deviceIds = audio.getDeviceIds();
    for (unsigned int deviceId : deviceIds) {
        RtAudio::DeviceInfo device = audio.getDeviceInfo(deviceId);
        if (device.name == driverName) {
            return initializeAudioDriver(RtAudio::WINDOWS_ASIO,
                                         channels,
                                         sampleRate,
                                         deviceId,
                                         deviceId,
                                         inputOffset,
                                         outputOffset);
        }
    }
    return -1;
#else
    (void)driverName;
    (void)channels;
    (void)sampleRate;
    (void)inputOffset;
    (void)outputOffset;
    return -1;
#endif
}

void releaseSelectedAsioDriver(void) {
    // Closes the ASIO driver retained after SelectAsioDriver.
#ifdef _WIN32
    if (g_isAsioDriverSelected)
        ASIOExit();
    if (asioDrivers != nullptr) {
        delete asioDrivers;
        asioDrivers = nullptr;
    }
    g_isAsioDriverSelected = false;
#endif
}

extern "C" PLUGIN_API unsigned int GetAudioDeviceCount(void) {
    // Returns devices currently discoverable through Windows WASAPI.
    RtAudio audio(RtAudio::WINDOWS_WASAPI);
    return audio.getDeviceCount();
}

extern "C" PLUGIN_API int GetAudioDeviceInfo(unsigned int deviceIndex,
                                              AudioDeviceInfo *outInfo) {
    // Copies one WASAPI device ID, name, and available channel counts.
    RtAudio audio(RtAudio::WINDOWS_WASAPI);
    std::vector<unsigned int> deviceIds = audio.getDeviceIds();
    if (deviceIndex >= deviceIds.size())
        return -1;

    RtAudio::DeviceInfo device = audio.getDeviceInfo(deviceIds[deviceIndex]);
    *outInfo = {};
    outInfo->id = device.ID;
    std::strncpy(outInfo->name, device.name.c_str(), sizeof(outInfo->name) - 1);
    outInfo->inputChannels = device.inputChannels;
    outInfo->outputChannels = device.outputChannels;
    outInfo->duplexChannels = device.duplexChannels;
    outInfo->isDefaultInput = device.isDefaultInput ? 1 : 0;
    outInfo->isDefaultOutput = device.isDefaultOutput ? 1 : 0;
    outInfo->preferredSampleRate = device.preferredSampleRate;
    return 0;
}

extern "C" PLUGIN_API int InitializeWithAudioDevice(unsigned int channels,
                                                      unsigned int sampleRate,
                                                      unsigned int inputDeviceId,
                                                      unsigned int outputDeviceId,
                                                      unsigned int inputOffset,
                                                      unsigned int outputOffset) {
    // Initializes through WASAPI using IDs returned by GetAudioDeviceInfo.
    return initializeAudioDriver(RtAudio::WINDOWS_WASAPI,
                                 channels,
                                 sampleRate,
                                 inputDeviceId,
                                 outputDeviceId,
                                 inputOffset,
                                 outputOffset);
}
