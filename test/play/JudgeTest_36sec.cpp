#include <mpg123.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "../../src/ChartParser.h"
#include "../../src/main.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

struct PluginApi {
#ifdef _WIN32
    HMODULE module;
#endif
    int (*InitializeJudgeTest)(unsigned int, unsigned int, const char *, float);
    int (*LoadChart)(const char *);
    int (*StartJudgeTestSession)(void);
    int (*FeedJudgeTestAudio)(const int16_t *, unsigned int);
    void (*SetJudgeOnsetBlockMs)(double);
    void (*SetOnsetSilence)(float);
    void (*SetOnsetCompression)(float);
    int (*PollJudgeEvent)(JudgeEvent *);
    void (*StopSession)(void);
    void (*Shutdown)(void);
};

struct JudgeSummary {
    unsigned int perfect;
    unsigned int good;
    unsigned int bad;
    unsigned int miss;
    unsigned int wrong;
};

class TeeStreamBuffer : public std::streambuf {
   public:
    // Writes one CLI stream to both its original console buffer and the test log.
    TeeStreamBuffer(std::streambuf *console, std::streambuf *log) : console_(console), log_(log) {}

   protected:
    int_type overflow(int_type character) override {
        if (traits_type::eq_int_type(character, traits_type::eof()))
            return traits_type::not_eof(character);
        char text = traits_type::to_char_type(character);
        if (traits_type::eq_int_type(console_->sputc(text), traits_type::eof()) ||
            traits_type::eq_int_type(log_->sputc(text), traits_type::eof()))
            return traits_type::eof();
        return character;
    }

    int sync() override {
        return console_->pubsync() == 0 && log_->pubsync() == 0 ? 0 : -1;
    }

    std::streamsize xsputn(const char *text, std::streamsize count) override {
        std::streamsize consoleCount = console_->sputn(text, count);
        std::streamsize logCount = log_->sputn(text, count);
        return std::min(consoleCount, logCount);
    }

   private:
    std::streambuf *console_;
    std::streambuf *log_;
};

void usage() {
    // Prints the optional audio, chart, DLL, and onset parameters for this replay test.
    std::cout << "usage: JudgeTest <guitarMp3Path> <chartPath> <dllPath> <onsetMethod> "
                 "<onsetThreshold> <onsetBlockMs> <onsetSilenceDb> <onsetCompression>\n";
    std::exit(0);
}

template <typename FunctionType>
bool loadPluginFunction(PluginApi *plugin, const char *name, FunctionType *outFunction) {
    // Loads one recorded-input judgment API from the native plugin DLL.
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
    // Opens the DLL and binds the APIs used to replay the guitar-only reference track.
#ifdef _WIN32
    *plugin = {};
    plugin->module = LoadLibraryA(dllPath.c_str());
    if (plugin->module == nullptr)
        return false;

    return loadPluginFunction(plugin, "InitializeJudgeTest", &plugin->InitializeJudgeTest) &&
           loadPluginFunction(plugin, "LoadChart", &plugin->LoadChart) &&
           loadPluginFunction(plugin, "StartJudgeTestSession", &plugin->StartJudgeTestSession) &&
           loadPluginFunction(plugin, "FeedJudgeTestAudio", &plugin->FeedJudgeTestAudio) &&
           loadPluginFunction(plugin, "SetJudgeOnsetBlockMs", &plugin->SetJudgeOnsetBlockMs) &&
           loadPluginFunction(plugin, "SetOnsetSilence", &plugin->SetOnsetSilence) &&
           loadPluginFunction(plugin, "SetOnsetCompression", &plugin->SetOnsetCompression) &&
           loadPluginFunction(plugin, "PollJudgeEvent", &plugin->PollJudgeEvent) &&
           loadPluginFunction(plugin, "StopSession", &plugin->StopSession) &&
           loadPluginFunction(plugin, "Shutdown", &plugin->Shutdown);
#else
    (void)dllPath;
    (void)plugin;
    return false;
#endif
}

bool decodeMp3(const std::string &path,
               std::vector<int16_t> *samples,
               unsigned int *sampleRate,
               unsigned int *channels) {
    // Decodes the guitar-only MP3 into the PCM format accepted by FeedJudgeTestAudio.
    if (mpg123_init() != MPG123_OK)
        return false;

    mpg123_handle *decoder = mpg123_new(nullptr, nullptr);
    if (decoder == nullptr || mpg123_open(decoder, path.c_str()) != MPG123_OK) {
        if (decoder != nullptr)
            mpg123_delete(decoder);
        mpg123_exit();
        return false;
    }

    mpg123_format_none(decoder);
    mpg123_format(decoder, 44100, MPG123_MONO | MPG123_STEREO, MPG123_ENC_SIGNED_16);
    long decodedRate = 0;
    int decodedChannels = 0;
    int encoding = 0;
    if (mpg123_getformat(decoder, &decodedRate, &decodedChannels, &encoding) != MPG123_OK ||
        encoding != MPG123_ENC_SIGNED_16) {
        mpg123_close(decoder);
        mpg123_delete(decoder);
        mpg123_exit();
        return false;
    }

    std::vector<int16_t> buffer(4096);
    size_t bytesRead = 0;
    int readResult = MPG123_OK;
    while ((readResult = mpg123_read(decoder,
                                     reinterpret_cast<unsigned char *>(buffer.data()),
                                     buffer.size() * sizeof(int16_t),
                                     &bytesRead)) == MPG123_OK ||
           readResult == MPG123_DONE) {
        samples->insert(
            samples->end(), buffer.begin(), buffer.begin() + bytesRead / sizeof(int16_t));
        if (readResult == MPG123_DONE)
            break;
    }

    mpg123_close(decoder);
    mpg123_delete(decoder);
    mpg123_exit();
    if (readResult != MPG123_DONE)
        return false;

    *sampleRate = (unsigned int)decodedRate;
    *channels = (unsigned int)decodedChannels;
    return true;
}

const char *judgeResultText(int result) {
    // Converts one plugin judgment code to the test report text.
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

void recordJudgeEvent(const JudgeEvent &event, JudgeSummary *summary) {
    // Prints one regular rhythm-game judgment from the reference replay.
    if (event.result == JudgeResult_Perfect)
        ++summary->perfect;
    else if (event.result == JudgeResult_Good)
        ++summary->good;
    else if (event.result == JudgeResult_Bad)
        ++summary->bad;
    else if (event.result == JudgeResult_Wrong)
        ++summary->wrong;
    else
        ++summary->miss;

    std::cout << event.noteIndex + 1 << ", " << judgeResultText(event.result) << ", "
              << event.noteName << ", " << event.judgedAudioTimeMs << ", " << event.errorMs << "\n";
}

int main(int argc, char *argv[]) {
    std::string mp3Path =
        std::string(PAK_SOURCE_DIR) + "/assets/songs/PAK - Night_ONLY_Rhythm_GT.mp3";
    std::string chartPath = std::string(PAK_SOURCE_DIR) + "/assets/charts/PAK - Night.json";
    std::string dllPath = PAK_DEFAULT_DLL_PATH;
    std::string onsetMethod = "default";
    float onsetThreshold = 0.04f;
    double onsetBlockMs = 100.0;
    float onsetSilenceDb = -45.0f;
    float onsetCompression = 0.0f;
    std::string onsetThresholdText = "0.04";
    std::string onsetBlockMsText = "100";
    std::string onsetSilenceDbText = "-45";
    std::string onsetCompressionText = "0";
    if (argc > 9)
        usage();
    if (argc > 1)
        mp3Path = argv[1];
    if (argc > 2)
        chartPath = argv[2];
    if (argc > 3)
        dllPath = argv[3];
    if (argc > 4)
        onsetMethod = argv[4];
    if (argc > 5) {
        onsetThreshold = std::strtof(argv[5], nullptr);
        onsetThresholdText = argv[5];
    }
    if (argc > 6) {
        onsetBlockMs = std::strtod(argv[6], nullptr);
        onsetBlockMsText = argv[6];
    }
    if (argc > 7) {
        onsetSilenceDb = std::strtof(argv[7], nullptr);
        onsetSilenceDbText = argv[7];
    }
    if (argc > 8) {
        onsetCompression = std::strtof(argv[8], nullptr);
        onsetCompressionText = argv[8];
    }

    std::string logPath = std::string(PAK_SOURCE_DIR) +
                          u8"/docs/JudgeStability/JudgeTest/JudgeTest_36sec_" + onsetMethod + "_" +
                          onsetThresholdText + "_" + onsetBlockMsText + "_" + onsetSilenceDbText +
                          "_" + onsetCompressionText + ".log";
    std::ofstream logFile(std::filesystem::u8path(logPath));
    if (!logFile) {
        std::cerr << "Failed to open test log: " << logPath << "\n";
        return 1;
    }
    std::streambuf *consoleOutput = std::cout.rdbuf();
    std::streambuf *consoleError = std::cerr.rdbuf();
    TeeStreamBuffer outputBuffer(consoleOutput, logFile.rdbuf());
    TeeStreamBuffer errorBuffer(consoleError, logFile.rdbuf());
    std::cout.rdbuf(&outputBuffer);
    std::cerr.rdbuf(&errorBuffer);
    std::cout << "Log: " << logPath << "\n";

    ChartParser::Chart chart = {};
    if (!ChartParser::loadChart(chartPath, chart)) {
        std::cerr << "Failed to load chart: " << chartPath << "\n";
        std::cout.rdbuf(consoleOutput);
        std::cerr.rdbuf(consoleError);
        return 1;
    }

    std::vector<int16_t> samples;
    unsigned int sampleRate = 0;
    unsigned int channels = 0;
    if (!decodeMp3(mp3Path, &samples, &sampleRate, &channels)) {
        std::cerr << "Failed to decode guitar reference: " << mp3Path << "\n";
        std::cout.rdbuf(consoleOutput);
        std::cerr.rdbuf(consoleError);
        return 1;
    }

    PluginApi plugin = {};
    if (!loadPlugin(dllPath, &plugin)) {
        std::cerr << "Failed to load plugin: " << dllPath << "\n";
        std::cout.rdbuf(consoleOutput);
        std::cerr.rdbuf(consoleError);
        return 1;
    }

    int result = 1;
    JudgeSummary summary = {};
    std::vector<int16_t> block(128 * channels, 0);
    auto nextBlockAt = std::chrono::steady_clock::now();
    if (plugin.InitializeJudgeTest(channels, sampleRate, onsetMethod.c_str(), onsetThreshold) !=
            0 ||
        plugin.LoadChart(chartPath.c_str()) != 0) {
        std::cerr << "Failed to start the guitar reference replay.\n";
        goto cleanup;
    }
    plugin.SetJudgeOnsetBlockMs(onsetBlockMs);
    plugin.SetOnsetSilence(onsetSilenceDb);
    plugin.SetOnsetCompression(onsetCompression);
    if (plugin.StartJudgeTestSession() != 0) {
        std::cerr << "Failed to start the guitar reference replay.\n";
        goto cleanup;
    }

    std::cout << "Replaying: " << mp3Path << "\n";
    std::cout << "Chart: " << chart.title << " / " << chart.difficulty << "\n";
    std::cout << "Onset block: " << onsetBlockMs << " ms\n";
    std::cout << "Onset silence: " << onsetSilenceDb << " dBFS\n";
    std::cout << "Onset compression: " << onsetCompression << "\n";
    std::cout << "Note, Judge, Chord, Onset(ms), Error(ms)\n";
    const size_t testSampleCount =
        std::min(samples.size(), static_cast<size_t>(sampleRate) * channels * 36);
    for (size_t offset = 0; offset < testSampleCount; offset += block.size()) {
        std::fill(block.begin(), block.end(), 0);
        size_t sampleCount = std::min(block.size(), testSampleCount - offset);
        std::memcpy(block.data(), samples.data() + offset, sampleCount * sizeof(int16_t));
        if (plugin.FeedJudgeTestAudio(block.data(), 128) != 0) {
            std::cerr << "Recorded input queue was full.\n";
            goto cleanup;
        }

        JudgeEvent event = {};
        while (plugin.PollJudgeEvent(&event) == 1) recordJudgeEvent(event, &summary);

        nextBlockAt += std::chrono::microseconds(128000000 / sampleRate);
        std::this_thread::sleep_until(nextBlockAt);
    }

    auto drainUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (nextBlockAt < drainUntil) {
        std::fill(block.begin(), block.end(), 0);
        if (plugin.FeedJudgeTestAudio(block.data(), 128) != 0) {
            std::cerr << "Recorded input queue was full.\n";
            goto cleanup;
        }
        nextBlockAt += std::chrono::microseconds(128000000 / sampleRate);
        std::this_thread::sleep_until(nextBlockAt);
    }
    {
        JudgeEvent event = {};
        while (plugin.PollJudgeEvent(&event) == 1) recordJudgeEvent(event, &summary);
    }

    std::cout << "Judge result | Perfect " << summary.perfect << " | Good " << summary.good
              << " | Bad " << summary.bad << " | Miss " << summary.miss << " | Wrong "
              << summary.wrong << "\n";
    std::cout << "Judged "
              << summary.perfect + summary.good + summary.bad + summary.miss + summary.wrong << "/"
              << chart.notes.size() << "\n";
    result = 0;

cleanup:
    plugin.StopSession();
    plugin.Shutdown();
#ifdef _WIN32
    FreeLibrary(plugin.module);
#endif
    std::cout.flush();
    std::cerr.flush();
    logFile.flush();
    std::cout.rdbuf(consoleOutput);
    std::cerr.rdbuf(consoleError);
    return result;
}
