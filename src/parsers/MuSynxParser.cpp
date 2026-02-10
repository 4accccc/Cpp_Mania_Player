#include "MuSynxParser.h"
#include "MuSynxSongDB.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <filesystem>

// Convert base-36 WAV ID to decimal index
static int base36ToInt(const std::string& s) {
    int result = 0;
    for (char c : s) {
        result *= 36;
        if (c >= '0' && c <= '9') {
            result += c - '0';
        } else if (c >= 'A' && c <= 'Z') {
            result += 10 + (c - 'A');
        } else if (c >= 'a' && c <= 'z') {
            result += 10 + (c - 'a');
        }
    }
    return result;
}

int64_t MuSynxParser::timeToMs(int64_t musynxTime) {
    // MUSYNX time unit is 0.1 microseconds (100 nanoseconds)
    // Divide by 10000 to get milliseconds
    return musynxTime / 10000;
}

int MuSynxParser::trackToLane(int track, int keyCount) {
    if (keyCount == 4) {
        // 4K: tracks 3,4,6,7 -> lanes 0,1,2,3
        switch (track) {
            case 3: return 0;
            case 4: return 1;
            case 6: return 2;
            case 7: return 3;
            default: return -1;
        }
    } else if (keyCount == 6) {
        // 6K: tracks 2-7 -> lanes 0-5
        if (track >= 2 && track <= 7) {
            return track - 2;
        }
        return -1;
    }
    return -1;
}

int MuSynxParser::getKeyCountFromFilename(const std::string& filename) {
    // Look for "4T" or "6T" in filename
    if (filename.find("4T") != std::string::npos) {
        return 4;
    } else if (filename.find("6T") != std::string::npos) {
        return 6;
    }
    return 4; // Default to 4K
}

std::string MuSynxParser::parseWavId(const std::string& id) {
    return id; // WAV ID is already a string
}

bool MuSynxParser::parse(const std::string& path, BeatmapInfo& info) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    // Get filename for key count detection
    std::filesystem::path filePath(path);
    std::string filename = filePath.filename().string();
    info.keyCount = getKeyCountFromFilename(filename);

    // Extract song name from filename (e.g., "silverTown4T_easy.txt" -> "silverTown")
    std::string songName = filename;
    size_t keyPos = songName.find("4T");
    if (keyPos == std::string::npos) keyPos = songName.find("6T");
    if (keyPos != std::string::npos) {
        songName = songName.substr(0, keyPos);
    }

    // Extract difficulty from filename
    std::string difficulty = "Normal";
    if (filename.find("_easy") != std::string::npos) difficulty = "Easy";
    else if (filename.find("_hard") != std::string::npos) difficulty = "Hard";
    else if (filename.find("_inf") != std::string::npos) difficulty = "Inferno";

    // Look up song title from database
    static auto songDB = getMuSynxSongDB();
    auto it = songDB.find(songName);
    if (it != songDB.end()) {
        info.title = it->second.title;
        info.artist = it->second.composer.empty() ? "MUSYNX" : it->second.composer;
    } else {
        info.title = songName;
        info.artist = "MUSYNX";
    }

    // Set metadata
    info.creator = "MUSYNX";
    info.version = difficulty;
    info.mode = 3; // mania mode
    info.od = 8.0f;
    info.hp = 8.0f;

    // WAV ID to HCA index mapping (WAV definitions appear in order matching ACB HCA order)
    std::unordered_map<std::string, int> wavIdToHcaIndex;
    std::unordered_map<std::string, std::string> wavFiles;  // WAV ID -> cue name
    int wavDefIndex = 0;  // Counter for WAV definitions
    std::string bgmWavId;  // WAV ID for BGM
    double bpm = 120.0;
    int64_t bgmStartTime = 0;  // BGM start time from MusicNote

    std::string line;
    while (std::getline(file, line)) {
        // Remove carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) continue;

        // Split by tab
        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string part;
        while (std::getline(ss, part, '\t')) {
            parts.push_back(part);
        }

        if (parts.empty()) continue;

        const std::string& cmd = parts[0];

        if (cmd == "BPM" && parts.size() >= 2) {
            bpm = std::stod(parts[1]);
            // Add timing point
            TimingPoint tp;
            tp.time = 0;
            tp.beatLength = 60000.0 / bpm;
            tp.uninherited = true;
            tp.effectiveBeatLength = tp.beatLength;
            tp.volume = 100;
            info.timingPoints.push_back(tp);
        }
        else if (cmd == "WAV" && parts.size() >= 3) {
            // WAV definitions appear in order matching ACB HCA extraction order
            wavIdToHcaIndex[parts[1]] = wavDefIndex++;
            wavFiles[parts[1]] = parts[2];
            if (parts[2] == "BGM") {
                bgmWavId = parts[1];
            }
        }
        else if (cmd == "Note" && parts.size() >= 4) {
            int64_t time = std::stoll(parts[1]);
            int track = std::stoi(parts[2]);
            std::string wavId = parts[3];

            int lane = trackToLane(track, info.keyCount);
            if (lane >= 0) {
                Note note(lane, timeToMs(time), false, 0);
                // Use cue name from WAV definition as filename
                auto nameIt = wavFiles.find(wavId);
                if (nameIt != wavFiles.end()) {
                    note.filename = nameIt->second;  // cue name like "kick_000"
                }
                info.notes.push_back(note);
            }
        }
        else if (cmd == "LongNote" && parts.size() >= 5) {
            int64_t startTime = std::stoll(parts[1]);
            int track = std::stoi(parts[2]);
            std::string wavId = parts[3];
            int64_t endTime = std::stoll(parts[4]);

            int lane = trackToLane(track, info.keyCount);
            if (lane >= 0) {
                Note note(lane, timeToMs(startTime), true, timeToMs(endTime));
                // Use cue name from WAV definition as filename
                auto nameIt = wavFiles.find(wavId);
                if (nameIt != wavFiles.end()) {
                    note.filename = nameIt->second;  // cue name like "kick_000"
                }
                info.notes.push_back(note);
            }
        }
        // MusicNote contains BGM start time
        else if (cmd == "MusicNote" && parts.size() >= 4) {
            std::string wavId = parts[3];
            if (wavId == bgmWavId && bgmStartTime == 0) {
                bgmStartTime = std::stoll(parts[1]);
            }
        }
    }

    // Adjust all note times relative to BGM start
    int64_t bgmOffsetMs = timeToMs(bgmStartTime);
    for (auto& note : info.notes) {
        note.time -= bgmOffsetMs;
        if (note.isHold) {
            note.endTime -= bgmOffsetMs;
        }
    }

    // Sort notes by time
    std::sort(info.notes.begin(), info.notes.end(),
        [](const Note& a, const Note& b) { return a.time < b.time; });

    // Calculate MD5 hash for replay matching
    // For now, use a simple hash based on note count and timing
    info.beatmapHash = std::to_string(info.notes.size()) + "_" + songName;

    return !info.notes.empty();
}

std::vector<std::string> MuSynxParser::extractCueNames(const std::string& path) {
    std::vector<std::string> cueNames;
    std::ifstream file(path);
    if (!file.is_open()) return cueNames;

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) continue;

        // Split by tab
        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string part;
        while (std::getline(ss, part, '\t')) {
            parts.push_back(part);
        }

        if (parts.size() >= 3 && parts[0] == "WAV") {
            cueNames.push_back(parts[2]);  // cue name like "BGM", "kick_000"
        }
    }
    return cueNames;
}
