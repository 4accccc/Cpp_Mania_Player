#include "VoxParser.h"
#include "../systems/SDVXSongDB.h"
#include "../core/MD5.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <filesystem>

static const int TICKS_PER_BEAT = 48;

int VoxParser::positionToTick(int measure, int beat, int offset,
                              const std::vector<TimeSignature>& timeSigs) {
    // Find active time signature at this measure
    // Default 4/4
    int beatsPerMeasure = 4;
    int currentMeasure = 1;
    int currentTick = 0;

    // timeSigs are sorted by tick; walk through them
    size_t tsIdx = 0;
    while (currentMeasure < measure) {
        // Check if next time sig change happens before target measure
        while (tsIdx + 1 < timeSigs.size()) {
            int nextTsTick = timeSigs[tsIdx + 1].tick;
            int ticksInCurrentMeasure = beatsPerMeasure * TICKS_PER_BEAT;
            if (currentTick + ticksInCurrentMeasure > nextTsTick) break;
            // Check if we'd pass the next time sig
            int measuresUntilNext = (nextTsTick - currentTick) / ticksInCurrentMeasure;
            if (currentMeasure + measuresUntilNext > measure) break;
            currentMeasure += measuresUntilNext;
            currentTick += measuresUntilNext * ticksInCurrentMeasure;
            tsIdx++;
            beatsPerMeasure = timeSigs[tsIdx].numerator;
            if (currentMeasure >= measure) break;
        }
        if (currentMeasure >= measure) break;
        currentTick += beatsPerMeasure * TICKS_PER_BEAT;
        currentMeasure++;
    }

    return currentTick + (beat - 1) * TICKS_PER_BEAT + offset;
}

int64_t VoxParser::tickToMs(int tick, const std::vector<BpmChange>& bpmChanges) {
    if (bpmChanges.empty()) return 0;

    double ms = 0.0;
    int prevTick = 0;
    double currentBpm = bpmChanges[0].bpm;

    for (size_t i = 1; i < bpmChanges.size(); i++) {
        if (bpmChanges[i].tick >= tick) break;
        int deltaTicks = bpmChanges[i].tick - prevTick;
        double msPerTick = 60000.0 / (currentBpm * TICKS_PER_BEAT);
        ms += deltaTicks * msPerTick;
        prevTick = bpmChanges[i].tick;
        currentBpm = bpmChanges[i].bpm;
    }

    int remainingTicks = tick - prevTick;
    double msPerTick = 60000.0 / (currentBpm * TICKS_PER_BEAT);
    ms += remainingTicks * msPerTick;

    return static_cast<int64_t>(std::round(ms));
}

int VoxParser::trackToLane(int trackIndex) {
    // trackIndex: 2=FX-L, 3=BT-A, 4=BT-B, 5=BT-C, 6=BT-D, 7=FX-R
    switch (trackIndex) {
        case 2: return 0;  // FX-L
        case 3: return 1;  // BT-A
        case 4: return 2;  // BT-B
        case 5: return 3;  // BT-C
        case 6: return 4;  // BT-D
        case 7: return 5;  // FX-R
        default: return -1;
    }
}

std::string VoxParser::getDifficultyName(const std::string& filename) {
    if (filename.find("_1n") != std::string::npos) return "NOV";
    if (filename.find("_2a") != std::string::npos) return "ADV";
    if (filename.find("_3e") != std::string::npos) return "EXH";
    if (filename.find("_4i") != std::string::npos) return "INF";  // renamed by infVer in Game.cpp
    if (filename.find("_5m") != std::string::npos) return "MXM";
    return "Unknown";
}

int VoxParser::getSongIdFromPath(const std::string& path) {
    std::filesystem::path p(path);
    std::string folder = p.parent_path().filename().string();
    // Folder format: {id}_{ascii}, e.g. "2348_l_ice"
    size_t underscore = folder.find('_');
    if (underscore != std::string::npos) {
        try { return std::stoi(folder.substr(0, underscore)); }
        catch (...) {}
    }
    return 0;
}

bool VoxParser::parse(const std::string& path, BeatmapInfo& info) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    info.keyCount = 6;
    info.mode = 3;
    info.od = 8.0f;
    info.hp = 8.0f;

    // Look up metadata from SongDB
    int songId = getSongIdFromPath(path);
    static auto songDB = getSDVXSongDB();
    auto it = songDB.find(songId);
    if (it != songDB.end()) {
        info.title = it->second.title;
        info.artist = it->second.artist;
    } else {
        // Fallback: use folder name
        std::filesystem::path p(path);
        info.title = p.parent_path().filename().string();
        info.artist = "SDVX";
    }

    info.creator = "SDVX";
    info.version = getDifficultyName(std::filesystem::path(path).filename().string());

    // Audio file: same folder, {id}_{ascii}.s3v
    {
        std::filesystem::path p(path);
        std::string folder = p.parent_path().filename().string();
        info.audioFilename = folder + ".s3v";
    }

    // Parse sections
    std::vector<BpmChange> bpmChanges;
    std::vector<TimeSignature> timeSigs;
    // Default 4/4
    timeSigs.push_back({0, 4, 4});

    std::string line;
    std::string currentSection;

    // Track data: trackIndex -> list of (tick, duration, param)
    struct RawNote {
        int tick;
        int duration;
        int param;
    };
    std::vector<RawNote> trackNotes[8]; // index 2-7 used

    while (std::getline(file, line)) {
        // Strip CR
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '/') continue;

        // Section headers
        if (line[0] == '#') {
            if (line == "#END") {
                currentSection.clear();
                continue;
            }
            currentSection = line;
            continue;
        }

        if (currentSection == "#BPM INFO") {
            int m, b, o;
            float bpm;
            int timeSigNum;
            if (sscanf(line.c_str(), "%d,%d,%d\t%f\t%d", &m, &b, &o, &bpm, &timeSigNum) >= 4) {
                int tick = positionToTick(m, b, o, timeSigs);
                bpmChanges.push_back({tick, (double)bpm});
            }
        }
        else if (currentSection == "#BEAT INFO") {
            int m, b, o, num, den;
            if (sscanf(line.c_str(), "%d,%d,%d\t%d\t%d", &m, &b, &o, &num, &den) >= 4) {
                int tick = positionToTick(m, b, o, timeSigs);
                timeSigs.push_back({tick, num, den > 0 ? den : 4});
                // Re-sort
                std::sort(timeSigs.begin(), timeSigs.end(),
                    [](const TimeSignature& a, const TimeSignature& b) { return a.tick < b.tick; });
            }
        }
        else if (currentSection == "#END POSITION") {
            // Not needed for note parsing
        }
        else {
            // Check TRACK2-7
            int trackIdx = -1;
            if (currentSection == "#TRACK2") trackIdx = 2;
            else if (currentSection == "#TRACK3") trackIdx = 3;
            else if (currentSection == "#TRACK4") trackIdx = 4;
            else if (currentSection == "#TRACK5") trackIdx = 5;
            else if (currentSection == "#TRACK6") trackIdx = 6;
            else if (currentSection == "#TRACK7") trackIdx = 7;

            if (trackIdx >= 2 && trackIdx <= 7) {
                int m, b, o, duration = 0, param = 0;
                if (sscanf(line.c_str(), "%d,%d,%d\t%d\t%d", &m, &b, &o, &duration, &param) >= 3) {
                    int tick = positionToTick(m, b, o, timeSigs);
                    trackNotes[trackIdx].push_back({tick, duration, param});
                }
            }
        }
    }

    // Sort BPM changes
    std::sort(bpmChanges.begin(), bpmChanges.end(),
        [](const BpmChange& a, const BpmChange& b) { return a.tick < b.tick; });

    if (bpmChanges.empty()) {
        bpmChanges.push_back({0, 120.0});
    }

    // Add timing points
    for (auto& bc : bpmChanges) {
        TimingPoint tp;
        tp.time = (double)tickToMs(bc.tick, bpmChanges);
        tp.beatLength = 60000.0 / bc.bpm;
        tp.uninherited = true;
        tp.effectiveBeatLength = tp.beatLength;
        tp.volume = 100;
        info.timingPoints.push_back(tp);
    }

    // Convert track notes to Note objects
    for (int t = 2; t <= 7; t++) {
        int lane = trackToLane(t);
        if (lane < 0) continue;

        for (auto& rn : trackNotes[t]) {
            int64_t timeMs = tickToMs(rn.tick, bpmChanges);
            if (rn.duration > 0) {
                // Hold note
                int64_t endMs = tickToMs(rn.tick + rn.duration, bpmChanges);
                Note note(lane, timeMs, true, endMs);
                info.notes.push_back(note);
            } else {
                // Chip (tap) note
                Note note(lane, timeMs, false, 0);
                info.notes.push_back(note);
            }
        }
    }

    // Sort notes by time
    std::sort(info.notes.begin(), info.notes.end(),
        [](const Note& a, const Note& b) { return a.time < b.time; });

    // MD5 hash
    info.beatmapHash = OsuParser::calculateMD5(path);

    return !info.notes.empty();
}
