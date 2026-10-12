#include "ChartParser.h"

#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

namespace {

int noteNameToMidi(const std::string &noteName) {
    // Converts a note name such as E2 or F#3 to a MIDI note number.
    int semitone = 0;
    switch (noteName[0]) {
    case 'C':
        semitone = 0;
        break;
    case 'D':
        semitone = 2;
        break;
    case 'E':
        semitone = 4;
        break;
    case 'F':
        semitone = 5;
        break;
    case 'G':
        semitone = 7;
        break;
    case 'A':
        semitone = 9;
        break;
    case 'B':
        semitone = 11;
        break;
    }

    size_t octaveIndex = 1;
    if (noteName.size() > 2 && noteName[1] == '#') {
        semitone++;
        octaveIndex = 2;
    } else if (noteName.size() > 2 && noteName[1] == 'b') {
        semitone--;
        octaveIndex = 2;
    }

    int octave = std::stoi(noteName.substr(octaveIndex));
    return (octave + 1) * 12 + semitone;
}

std::string midiToNoteName(int midi) {
    // Converts a MIDI note number to note name with octave.
    const char *noteNames[] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B",
    };
    int octave = midi / 12 - 1;
    return std::string(noteNames[midi % 12]) + std::to_string(octave);
}

int guitarNoteToMidi(const std::vector<std::string> &tuning, int stringNumber, int fret) {
    // Calculates the played MIDI note from guitar string number and fret.
    int tuningIndex = (int)tuning.size() - stringNumber;
    return noteNameToMidi(tuning[tuningIndex]) + fret;
}

int tickToMs(int tick, double bpm, int resolution, int audioOffsetMs = 0) {
    // Converts chart ticks to milliseconds from audio start.
    double tickMs = 60000.0 / bpm / resolution;
    return audioOffsetMs + (int)std::round(tick * tickMs);
}

}

namespace ChartParser {

bool loadChart(const std::string &path, Chart &chart) {
    // Parses a v1 chart file into rhythm-game timing and pitch data.
    std::ifstream file(path);
    if (!file)
        return false;

    nlohmann::json root;
    file >> root;

    const nlohmann::json &song = root["song"];
    const nlohmann::json &track = root["track"];

    chart = {};
    chart.schemaVersion = root["schemaVersion"].get<std::string>();
    chart.songId = song["songId"].get<std::string>();
    chart.title = song["title"].get<std::string>();
    chart.artist = song["artist"].get<std::string>();
    chart.bpm = song["bpm"].get<double>();
    chart.resolution = song["resolution"].get<int>();
    chart.timeSignature = song["timeSignature"].get<std::string>();
    chart.durationMs = song["durationMs"].get<int>();
    chart.audioFile = song["audioFile"].get<std::string>();
    chart.audioOffsetMs = song["audioOffsetMs"].get<int>();

    chart.trackId = track["trackId"].get<std::string>();
    chart.instrument = track["instrument"].get<std::string>();
    chart.difficulty = track["difficulty"].get<std::string>();
    chart.tuning = track["tuning"].get<std::vector<std::string>>();

    for (const nlohmann::json &noteJson : root["notes"]) {
        ChartNote note = {};
        note.startTick = noteJson["startTick"].get<int>();
        note.durationTick = noteJson["durationTick"].get<int>();
        note.stringNumber = noteJson["string"].get<int>();
        note.fret = noteJson["fret"].get<int>();
        note.finger = noteJson["finger"].get<int>();
        note.technique = noteJson["technique"].get<std::string>();
        note.startMs = tickToMs(note.startTick, chart.bpm, chart.resolution, chart.audioOffsetMs);
        note.durationMs = tickToMs(note.durationTick, chart.bpm, chart.resolution);
        note.midi = guitarNoteToMidi(chart.tuning, note.stringNumber, note.fret);
        note.noteName = midiToNoteName(note.midi);
        chart.notes.push_back(note);
    }

    return true;
}

}
