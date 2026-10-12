#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <mpg123.h>

#include "../../src/ChartParser.h"
#include "../../src/main.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

constexpr int kTargetCount = 24;
constexpr double kLabelMatchToleranceMs = 75.0;

struct StrokeLabel {
    double timeMs;
    std::string direction;
    std::string strength;
    bool matched;
};

struct DetectionStats {
    unsigned int truePositive = 0;
    unsigned int falseNegative = 0;
};

bool isUnintendedLabel(const StrokeLabel &label) {
    // Identifies recorded but intentionally excluded sounds marked as x,x in the CSV.
    return label.direction == "x" && label.strength == "x";
}

void trimSpaces(std::string *text) {
    // Removes CSV field padding before comparing direction and picking strength labels.
    size_t first = 0;
    while (first < text->size() && std::isspace((unsigned char)(*text)[first]))
        ++first;

    size_t last = text->size();
    while (last > first && std::isspace((unsigned char)(*text)[last - 1]))
        --last;
    *text = text->substr(first, last - first);
}

bool parseLabelTimeMs(const std::string &text, double *timeMs) {
    // Converts the CSV HH:MM:SS.mmm timestamp to an audio stream time.
    int hours = 0;
    int minutes = 0;
    double seconds = 0.0;
    if (std::sscanf(text.c_str(), "%d:%d:%lf", &hours, &minutes, &seconds) != 3)
        return false;

    *timeMs = ((hours * 60.0 + minutes) * 60.0 + seconds) * 1000.0;
    return true;
}

bool loadStrokeLabels(const std::string &path, std::vector<StrokeLabel> *labels) {
    // Loads expected stroke timing, direction, and picking strength from the CSV label file.
    std::ifstream file(std::filesystem::u8path(path));
    if (!file)
        return false;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream row(line);
        std::string timeText;
        std::string direction;
        std::string strength;
        if (!std::getline(row, timeText, ',') || !std::getline(row, direction, ',') ||
            !std::getline(row, strength, ','))
            return false;

        trimSpaces(&direction);
        trimSpaces(&strength);

        double timeMs = 0.0;
        if (!parseLabelTimeMs(timeText, &timeMs))
            return false;
        labels->push_back({timeMs, direction, strength, false});
    }
    return !labels->empty();
}

int findMatchingLabel(const std::vector<StrokeLabel> &labels, double onsetTimeMs) {
    // Finds the nearest unused label inside the onset-to-label match tolerance.
    int matchIndex = -1;
    double bestDistanceMs = kLabelMatchToleranceMs;
    for (int index = 0; index < (int)labels.size(); ++index) {
        if (labels[index].matched)
            continue;

        double distanceMs = std::abs(onsetTimeMs - labels[index].timeMs);
        if (distanceMs <= bestDistanceMs) {
            bestDistanceMs = distanceMs;
            matchIndex = index;
        }
    }
    return matchIndex;
}

void printStats(const char *name, const DetectionStats &stats, unsigned int falsePositive) {
    // Prints TP, FP, and FN counts for all strokes or one label category.
    std::cout << name << " | TP " << stats.truePositive << " | FP " << falsePositive
              << " | FN " << stats.falseNegative << "\n";
}

void recordRawOnset(const FingeringTestRawOnset &onset,
                    std::vector<StrokeLabel> *labels,
                    DetectionStats *stats,
                    unsigned int *falsePositive,
                    unsigned int *ignoredOnsets,
                    unsigned int *startedJudgments,
                    unsigned int *droppedDuringPending) {
    // Matches one raw detector onset to a label before the judgment-pending filter can discard it.
    if (onset.startedJudgment)
        ++*startedJudgments;
    else
        ++*droppedDuringPending;

    int labelIndex = findMatchingLabel(*labels, onset.audioTimeMs);
    std::cout << "Raw onset | " << std::fixed << std::setprecision(1) << onset.audioTimeMs
              << " ms | " << (onset.startedJudgment ? "Started" : "Dropped");
    if (labelIndex < 0) {
        ++*falsePositive;
        std::cout << " | Raw FP\n";
        return;
    }

    StrokeLabel &label = (*labels)[labelIndex];
    label.matched = true;
    if (isUnintendedLabel(label)) {
        ++*ignoredOnsets;
        std::cout << " | Ignored x x"
                  << " | offset " << onset.audioTimeMs - label.timeMs << " ms\n";
        return;
    }

    ++stats->truePositive;
    std::cout << " | Raw TP " << label.direction << " " << label.strength
              << " | offset " << onset.audioTimeMs - label.timeMs << " ms\n";
}

struct PluginApi {
#ifdef _WIN32
    HMODULE module;
#endif
    int (*InitializeFingeringTest)(unsigned int, unsigned int, const char *, float);
    int (*LoadChart)(const char *);
    int (*StartFingeringTestSession)(void);
    int (*FeedFingeringTestAudio)(const int16_t *, unsigned int);
    int (*PollJudgeEvent)(JudgeEvent *);
    int (*PollFingeringTestRawOnset)(FingeringTestRawOnset *);
    int (*GetJudgmentDiagnostics)(JudgmentDiagnostics *);
    void (*StopSession)(void);
    void (*Shutdown)(void);
};

void usage(void) {
    // Prints the optional paths accepted by the recorded G5 fingering test.
    std::cout << "usage: G5fingeringTest <mp3Path> <chartPath> <dllPath> <onsetMethod> "
                 "<onsetThreshold> <labelCsvPath>\n";
    std::exit(0);
}

template <typename FunctionType>
bool loadPluginFunction(PluginApi *plugin, const char *name, FunctionType *outFunction) {
    // Loads one recorded-input test API from the native plugin DLL.
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
    // Opens the plugin and binds the APIs needed to replay recorded input.
#ifdef _WIN32
    *plugin = {};
    plugin->module = LoadLibraryA(dllPath.c_str());
    if (plugin->module == nullptr)
        return false;

    return loadPluginFunction(plugin, "InitializeFingeringTest", &plugin->InitializeFingeringTest) &&
           loadPluginFunction(plugin, "LoadChart", &plugin->LoadChart) &&
           loadPluginFunction(plugin, "StartFingeringTestSession", &plugin->StartFingeringTestSession) &&
           loadPluginFunction(plugin, "FeedFingeringTestAudio", &plugin->FeedFingeringTestAudio) &&
           loadPluginFunction(plugin, "PollJudgeEvent", &plugin->PollJudgeEvent) &&
           loadPluginFunction(plugin,
                              "PollFingeringTestRawOnset",
                              &plugin->PollFingeringTestRawOnset) &&
           loadPluginFunction(plugin, "GetJudgmentDiagnostics", &plugin->GetJudgmentDiagnostics) &&
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
    // Decodes the recorded guitar input as signed 16-bit interleaved samples.
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
        samples->insert(samples->end(), buffer.begin(), buffer.begin() + bytesRead / sizeof(int16_t));
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

int main(int argc, char *argv[]) {
    std::string mp3Path = std::string(PAK_SOURCE_DIR) + "/assets/testSound/G5stroke_gainUp.mp3";
    std::string chartPath = std::string(PAK_SOURCE_DIR) + "/assets/charts/PAK - Night.json";
    std::string dllPath = PAK_DEFAULT_DLL_PATH;
    std::string onsetMethod = "default";
    float onsetThreshold = 0.2f;
    std::string labelCsvPath = std::string(PAK_SOURCE_DIR) +
                               u8"/docs/\uD310\uC815 \uC548\uC815\uC131/G5stroke_label.csv";
    if (argc > 7)
        usage();
    if (argc > 1)
        mp3Path = argv[1];
    if (argc > 2)
        chartPath = argv[2];
    if (argc > 3)
        dllPath = argv[3];
    if (argc > 4)
        onsetMethod = argv[4];
    if (argc > 5)
        onsetThreshold = std::strtof(argv[5], nullptr);
    if (argc > 6)
        labelCsvPath = argv[6];

    std::vector<StrokeLabel> labels;
    if (!loadStrokeLabels(labelCsvPath, &labels)) {
        std::cerr << "Failed to load stroke labels: " << labelCsvPath << "\n";
        return 1;
    }
    std::vector<StrokeLabel> rawLabels = labels;

    ChartParser::Chart chart = {};
    if (!ChartParser::loadChart(chartPath, chart)) {
        std::cerr << "Failed to load chart: " << chartPath << "\n";
        return 1;
    }
    if (chart.notes.size() < kTargetCount) {
        std::cerr << "The chart contains " << chart.notes.size() << " targets; expected at least "
                  << kTargetCount << ".\n";
        return 1;
    }
    for (int index = 0; index < kTargetCount; ++index) {
        if (chart.notes[index].interpretation != "chord" || chart.notes[index].noteName != "G5") {
            std::cerr << "Target " << index + 1 << " is not G5.\n";
            return 1;
        }
    }

    std::vector<int16_t> recordedSamples;
    unsigned int sampleRate = 0;
    unsigned int channels = 0;
    if (!decodeMp3(mp3Path, &recordedSamples, &sampleRate, &channels)) {
        std::cerr << "Failed to decode recorded input: " << mp3Path << "\n";
        return 1;
    }

    PluginApi plugin = {};
    if (!loadPlugin(dllPath, &plugin)) {
        std::cerr << "Failed to load plugin: " << dllPath << "\n";
        return 1;
    }

    std::vector<int16_t> block(128 * channels, 0);
    unsigned int correctTargets = 0;
    std::vector<bool> passedTargets(kTargetCount, false);
    unsigned int totalEvents = 0;
    unsigned int falsePositive = 0;
    unsigned int ignoredOnsets = 0;
    unsigned int rawFalsePositive = 0;
    unsigned int rawIgnoredOnsets = 0;
    unsigned int rawStartedJudgments = 0;
    unsigned int rawDroppedDuringPending = 0;
    DetectionStats allStats = {};
    DetectionStats rawStats = {};
    DetectionStats downStats = {};
    DetectionStats upStats = {};
    DetectionStats strongStats = {};
    DetectionStats weakStats = {};
    double previousOnsetAudioTimeMs = -1.0;
    auto nextBlockAt = std::chrono::steady_clock::now();
    JudgeEvent event = {};
    FingeringTestRawOnset rawOnset = {};
    int result = 1;
    std::cout << "Onset method: " << onsetMethod << "\n";
    std::cout << "Onset threshold: " << onsetThreshold << "\n";
    int initializeResult =
        plugin.InitializeFingeringTest(channels, sampleRate, onsetMethod.c_str(), onsetThreshold);
    int loadChartResult = initializeResult == 0 ? plugin.LoadChart(chartPath.c_str()) : -1;
    int startSessionResult = loadChartResult == 0 ? plugin.StartFingeringTestSession() : -1;
    if (initializeResult != 0 || loadChartResult != 0 || startSessionResult != 0) {
        std::cerr << "Failed to start recorded fingering practice | initialize " << initializeResult
                  << " | chart " << loadChartResult << " | session " << startSessionResult << "\n";
        goto cleanup;
    }

    for (size_t offset = 0; offset < recordedSamples.size();
         offset += block.size()) {
        std::fill(block.begin(), block.end(), 0);
        size_t sampleCount = std::min(block.size(), recordedSamples.size() - offset);
        std::memcpy(block.data(), recordedSamples.data() + offset, sampleCount * sizeof(int16_t));
        if (plugin.FeedFingeringTestAudio(block.data(), 128) != 0) {
            std::cerr << "Recorded input queue was full.\n";
            goto cleanup;
        }

        while (plugin.PollFingeringTestRawOnset(&rawOnset) == 1) {
            recordRawOnset(rawOnset,
                           &rawLabels,
                           &rawStats,
                           &rawFalsePositive,
                           &rawIgnoredOnsets,
                           &rawStartedJudgments,
                           &rawDroppedDuringPending);
        }

        while (plugin.PollJudgeEvent(&event) == 1) {
            ++totalEvents;
            if (event.noteIndex < kTargetCount) {
                int labelIndex = findMatchingLabel(labels, event.judgedAudioTimeMs);
                std::cout << "Onset " << totalEvents << " | Target " << event.noteIndex + 1
                          << ": "
                          << (event.result == JudgeResult_Perfect ? "Correct" : "Incorrect")
                          << " | " << std::fixed << std::setprecision(1)
                          << event.judgedAudioTimeMs << " ms";
                if (labelIndex >= 0) {
                    StrokeLabel &label = labels[labelIndex];
                    label.matched = true;
                    if (isUnintendedLabel(label)) {
                        ++ignoredOnsets;
                        std::cout << " | Ignored x x"
                                  << " | offset " << event.judgedAudioTimeMs - label.timeMs << " ms";
                    } else {
                        ++allStats.truePositive;
                        DetectionStats &directionStats = label.direction == "down" ? downStats : upStats;
                        DetectionStats &strengthStats = label.strength == "strong" ? strongStats : weakStats;
                        ++directionStats.truePositive;
                        ++strengthStats.truePositive;
                        std::cout << " | TP " << label.direction << " " << label.strength
                                  << " | offset " << event.judgedAudioTimeMs - label.timeMs << " ms";
                    }
                } else {
                    ++falsePositive;
                    std::cout << " | FP";
                }
                if (previousOnsetAudioTimeMs >= 0.0)
                    std::cout << " | gap "
                              << event.judgedAudioTimeMs - previousOnsetAudioTimeMs << " ms";
                std::cout << "\n";
                previousOnsetAudioTimeMs = event.judgedAudioTimeMs;
                if (event.result == JudgeResult_Perfect && !passedTargets[event.noteIndex]) {
                    passedTargets[event.noteIndex] = true;
                    ++correctTargets;
                }
            }
        }
        nextBlockAt += std::chrono::microseconds(128000000 / sampleRate);
        std::this_thread::sleep_until(nextBlockAt);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    while (plugin.PollFingeringTestRawOnset(&rawOnset) == 1) {
        recordRawOnset(rawOnset,
                       &rawLabels,
                       &rawStats,
                       &rawFalsePositive,
                       &rawIgnoredOnsets,
                       &rawStartedJudgments,
                       &rawDroppedDuringPending);
    }
    while (plugin.PollJudgeEvent(&event) == 1) {
        ++totalEvents;
        if (event.noteIndex < kTargetCount) {
            int labelIndex = findMatchingLabel(labels, event.judgedAudioTimeMs);
            std::cout << "Onset " << totalEvents << " | Target " << event.noteIndex + 1 << ": "
                      << (event.result == JudgeResult_Perfect ? "Correct" : "Incorrect")
                      << " | " << std::fixed << std::setprecision(1)
                      << event.judgedAudioTimeMs << " ms";
            if (labelIndex >= 0) {
                StrokeLabel &label = labels[labelIndex];
                label.matched = true;
                if (isUnintendedLabel(label)) {
                    ++ignoredOnsets;
                    std::cout << " | Ignored x x"
                              << " | offset " << event.judgedAudioTimeMs - label.timeMs << " ms";
                } else {
                    ++allStats.truePositive;
                    DetectionStats &directionStats = label.direction == "down" ? downStats : upStats;
                    DetectionStats &strengthStats = label.strength == "strong" ? strongStats : weakStats;
                    ++directionStats.truePositive;
                    ++strengthStats.truePositive;
                    std::cout << " | TP " << label.direction << " " << label.strength
                              << " | offset " << event.judgedAudioTimeMs - label.timeMs << " ms";
                }
            } else {
                ++falsePositive;
                std::cout << " | FP";
            }
            if (previousOnsetAudioTimeMs >= 0.0)
                std::cout << " | gap " << event.judgedAudioTimeMs - previousOnsetAudioTimeMs
                          << " ms";
            std::cout << "\n";
            previousOnsetAudioTimeMs = event.judgedAudioTimeMs;
            if (event.result == JudgeResult_Perfect && !passedTargets[event.noteIndex]) {
                passedTargets[event.noteIndex] = true;
                ++correctTargets;
            }
        }
    }

    {
        JudgmentDiagnostics diagnostics = {};
        plugin.GetJudgmentDiagnostics(&diagnostics);
        std::cout << "Result: " << correctTargets << "/" << kTargetCount
                  << " targets correct, " << totalEvents << " judge events"
                  << " | onset " << diagnostics.detectedOnsets
                  << " | started " << diagnostics.startedFingeringJudgments
                  << " | chord pass " << diagnostics.passedChordJudgments
                  << " | chord fail " << diagnostics.failedChordJudgments << "\n";
    }
    for (const StrokeLabel &label : rawLabels) {
        if (label.matched || isUnintendedLabel(label))
            continue;

        ++rawStats.falseNegative;
        std::cout << "Raw FN | " << std::fixed << std::setprecision(1) << label.timeMs
                  << " ms | " << label.direction << " " << label.strength << "\n";
    }
    for (const StrokeLabel &label : labels) {
        if (label.matched || isUnintendedLabel(label))
            continue;

        ++allStats.falseNegative;
        DetectionStats &directionStats = label.direction == "down" ? downStats : upStats;
        DetectionStats &strengthStats = label.strength == "strong" ? strongStats : weakStats;
        ++directionStats.falseNegative;
        ++strengthStats.falseNegative;
        std::cout << "FN | " << std::fixed << std::setprecision(1) << label.timeMs << " ms | "
                  << label.direction << " " << label.strength << "\n";
    }
    std::cout << "Label match tolerance: +/-" << kLabelMatchToleranceMs << " ms\n";
    std::cout << "Raw onset delivery | started " << rawStartedJudgments << " | dropped "
              << rawDroppedDuringPending << " | ignored " << rawIgnoredOnsets << "\n";
    printStats("Raw", rawStats, rawFalsePositive);
    std::cout << "Ignored unintended onsets: " << ignoredOnsets << "\n";
    printStats("All", allStats, falsePositive);
    printStats("Direction down", downStats, 0);
    printStats("Direction up", upStats, 0);
    printStats("Strength strong", strongStats, 0);
    printStats("Strength weak", weakStats, 0);
    result = 0;

cleanup:
    plugin.StopSession();
    plugin.Shutdown();
#ifdef _WIN32
    FreeLibrary(plugin.module);
#endif
    return result;
}
