/******************************************/
/*
  duplex.cpp
  by Gary P. Scavone, 2006-2019.

  This program opens a duplex stream and passes
  input directly through to the output.
*/
/******************************************/

#include <cstdint>
#include <cstring>
#include <iostream>

#include "RtAudio.h"

/*
typedef int8_t MY_TYPE;
#define FORMAT RTAUDIO_SINT8
*/

typedef int16_t MY_TYPE;
#define FORMAT RTAUDIO_SINT16

/*
typedef S24 MY_TYPE;
#define FORMAT RTAUDIO_SINT24

typedef int32_t MY_TYPE;
#define FORMAT RTAUDIO_SINT32

typedef float MY_TYPE;
#define FORMAT RTAUDIO_FLOAT32

typedef double MY_TYPE;
#define FORMAT RTAUDIO_FLOAT64
*/

double streamTimePrintIncrement = 1.0;  // seconds
double streamTimePrintTime = 1.0;       // seconds

int inout(void *outputBuffer,
          void *inputBuffer,
          unsigned int /*nBufferFrames*/,
          double streamTime,
          RtAudioStreamStatus status,
          void *data) {
    // Since the number of input and output channels is equal, we can do
    // a simple buffer copy operation here.
    if (status)
        std::cout << "Stream over/underflow detected." << std::endl;

    if (streamTime >= streamTimePrintTime) {
        std::cout << "streamTime = " << streamTime << std::endl;
        streamTimePrintTime += streamTimePrintIncrement;
    }

    unsigned int *bytes = (unsigned int *)data;
    memcpy(outputBuffer, inputBuffer, *bytes);
    return 0;
}

#if defined(_WIN32)
#define PLUGIN_API __declspec(dllexport)
#else
#define PLUGIN_API
#endif

static RtAudio *g_adac = nullptr;
static unsigned int g_bufferFrames = 512;
static unsigned int g_bufferBytes = 0;

extern "C" PLUGIN_API int DuplexPlugin_Init(unsigned int channels,
                                            unsigned int sampleRate,
                                            unsigned int inputDevice,
                                            unsigned int outputDevice,
                                            unsigned int inputOffset,
                                            unsigned int outputOffset);
extern "C" PLUGIN_API int DuplexPlugin_Start(void);
extern "C" PLUGIN_API void DuplexPlugin_Stop(void);
extern "C" PLUGIN_API void DuplexPlugin_Shutdown(void);

extern "C" PLUGIN_API int DuplexPlugin_Init(unsigned int channels,
                                            unsigned int sampleRate,
                                            unsigned int inputDevice,
                                            unsigned int outputDevice,
                                            unsigned int inputOffset,
                                            unsigned int outputOffset) {
    DuplexPlugin_Shutdown();

    g_adac = new RtAudio();
    g_adac->showWarnings(true);

    // Set the same number of channels for both input and output.
    RtAudio::StreamParameters iParams, oParams;
    iParams.nChannels = channels;
    iParams.firstChannel = inputOffset;
    oParams.nChannels = channels;
    oParams.firstChannel = outputOffset;

    if (inputDevice == 0) {
        iParams.deviceId = g_adac->getDefaultInputDevice();
    } else {
        std::vector<unsigned int> deviceIds = g_adac->getDeviceIds();
        iParams.deviceId = deviceIds[inputDevice];
    }

    if (outputDevice == 0) {
        oParams.deviceId = g_adac->getDefaultOutputDevice();
    } else {
        std::vector<unsigned int> deviceIds = g_adac->getDeviceIds();
        oParams.deviceId = deviceIds[outputDevice];
    }

    g_bufferBytes = g_bufferFrames * channels * sizeof(MY_TYPE);
    if (g_adac->openStream(&oParams,
                           &iParams,
                           FORMAT,
                           sampleRate,
                           &g_bufferFrames,
                           &inout,
                           (void *)&g_bufferBytes,
                           nullptr)) {
        DuplexPlugin_Shutdown();
        return -1;
    }

    return 0;
}

extern "C" PLUGIN_API int DuplexPlugin_Start(void) {
    if (g_adac == nullptr)
        return -1;
    if (g_adac->isStreamOpen() == false)
        return -1;
    if (g_adac->startStream())
        return -1;
    return 0;
}

extern "C" PLUGIN_API void DuplexPlugin_Stop(void) {
    if (g_adac == nullptr)
        return;
    if (g_adac->isStreamRunning())
        g_adac->stopStream();
}

extern "C" PLUGIN_API void DuplexPlugin_Shutdown(void) {
    if (g_adac == nullptr)
        return;
    if (g_adac->isStreamRunning())
        g_adac->stopStream();
    if (g_adac->isStreamOpen())
        g_adac->closeStream();
    delete g_adac;
    g_adac = nullptr;
}
