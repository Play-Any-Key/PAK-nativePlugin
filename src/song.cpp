#include "song.h"

#include "plugin_state.h"

#include <cstring>
#include <filesystem>

#include <mpg123.h>

static bool g_songDecoderInitialized = false;

bool initializeSongDecoder(void) {
    // Initializes mpg123 once for the native song player.
    if (g_songDecoderInitialized)
        return true;

    g_songDecoderInitialized = mpg123_init() == MPG123_OK;
    return g_songDecoderInitialized;
}

bool loadSong(PluginState *state, const char *chartPath) {
    // Decodes the chart song as PCM at the RtAudio stream sample rate.
    std::filesystem::path chartDirectory = std::filesystem::path(chartPath).parent_path();
    std::filesystem::path audioPath = chartDirectory / state->chart.audioFile;
    if (!std::filesystem::exists(audioPath))
        audioPath = chartDirectory.parent_path() / "songs" / state->chart.audioFile;

    mpg123_handle *decoder = mpg123_new(nullptr, nullptr);
    if (decoder == nullptr)
        return false;

    mpg123_format_none(decoder);
    mpg123_format(decoder,
                  (long)state->sampleRate,
                  MPG123_MONO | MPG123_STEREO,
                  MPG123_ENC_SIGNED_16);
    if (mpg123_open(decoder, audioPath.string().c_str()) != MPG123_OK) {
        mpg123_delete(decoder);
        return false;
    }

    long sampleRate = 0;
    int channels = 0;
    int encoding = 0;
    if (mpg123_getformat(decoder, &sampleRate, &channels, &encoding) != MPG123_OK
        || sampleRate != (long)state->sampleRate || encoding != MPG123_ENC_SIGNED_16) {
        mpg123_close(decoder);
        mpg123_delete(decoder);
        return false;
    }

    state->songSamples.clear();
    unsigned char buffer[8192];
    size_t bytesRead = 0;
    int readResult = MPG123_OK;
    while ((readResult = mpg123_read(decoder, buffer, sizeof(buffer), &bytesRead)) == MPG123_OK) {
        size_t sampleCount = bytesRead / sizeof(MY_TYPE);
        size_t oldSize = state->songSamples.size();
        state->songSamples.resize(oldSize + sampleCount);
        std::memcpy(state->songSamples.data() + oldSize, buffer, bytesRead);
    }

    mpg123_close(decoder);
    mpg123_delete(decoder);
    if (readResult != MPG123_DONE)
        return false;

    state->songChannels = (unsigned int)channels;
    state->songFrames = state->songSamples.size() / state->songChannels;
    return state->songFrames > 0;
}

bool loadMetronome(PluginState *state, const char *chartPath) {
    // Decodes the bundled metronome sound at the RtAudio stream sample rate.
    std::filesystem::path chartDirectory = std::filesystem::path(chartPath).parent_path();
    std::filesystem::path audioPath = chartDirectory.parent_path() / "SFX" /
                                      "freesound_community-drumsticks-pro-mark-la-special-2bn-hickory-no4-103712.mp3";

    mpg123_handle *decoder = mpg123_new(nullptr, nullptr);
    if (decoder == nullptr)
        return false;

    mpg123_format_none(decoder);
    mpg123_format(decoder,
                  (long)state->sampleRate,
                  MPG123_MONO | MPG123_STEREO,
                  MPG123_ENC_SIGNED_16);
    if (mpg123_open(decoder, audioPath.string().c_str()) != MPG123_OK) {
        mpg123_delete(decoder);
        return false;
    }

    long sampleRate = 0;
    int channels = 0;
    int encoding = 0;
    if (mpg123_getformat(decoder, &sampleRate, &channels, &encoding) != MPG123_OK
        || sampleRate != (long)state->sampleRate || encoding != MPG123_ENC_SIGNED_16) {
        mpg123_close(decoder);
        mpg123_delete(decoder);
        return false;
    }

    state->metronomeSamples.clear();
    unsigned char buffer[8192];
    size_t bytesRead = 0;
    int readResult = MPG123_OK;
    while ((readResult = mpg123_read(decoder, buffer, sizeof(buffer), &bytesRead)) == MPG123_OK) {
        size_t sampleCount = bytesRead / sizeof(MY_TYPE);
        size_t oldSize = state->metronomeSamples.size();
        state->metronomeSamples.resize(oldSize + sampleCount);
        std::memcpy(state->metronomeSamples.data() + oldSize, buffer, bytesRead);
    }

    mpg123_close(decoder);
    mpg123_delete(decoder);
    if (readResult != MPG123_DONE)
        return false;

    state->metronomeChannels = (unsigned int)channels;
    state->metronomeFrames = state->metronomeSamples.size() / state->metronomeChannels;
    return state->metronomeFrames > 0;
}

void shutdownSongDecoder(void) {
    // Releases mpg123 after the native song player stops.
    if (!g_songDecoderInitialized)
        return;

    mpg123_exit();
    g_songDecoderInitialized = false;
}
