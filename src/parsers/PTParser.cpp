#include "PTParser.h"
#include "DJMaxSongDB.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <iostream>

namespace {
    constexpr int MEASURE_LENGTH = 192;
    constexpr uint8_t PT_EVTT_NOTE = 1;
    constexpr uint8_t PT_EVTT_TEMPO = 3;
    constexpr size_t OGG_RECORD_LENGTH = 0x42;  // 66 bytes
    constexpr size_t EZTR_HEADER_LENGTH = 0x4e; // 78 bytes
    constexpr size_t EVENT_RECORD_LENGTH = 0x0b; // 11 bytes
}

bool PTParser::isPTFile(const std::string& filepath) {
    std::filesystem::path p(filepath);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext != ".pt") return false;

    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    char magic[4];
    file.read(magic, 4);
    return (magic[0] == 'P' && magic[1] == 'T' && magic[2] == 'F' && magic[3] == 'F');
}

int PTParser::detectKeyCount(const std::string& filename) {
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.find("7key") != std::string::npos ||
        lower.find("_7k") != std::string::npos ||
        lower.find("7k_") != std::string::npos) {
        return 7;
    }
    return 5;  // Default to 5K
}

int64_t PTParser::positionToMs(uint16_t pos, float bpm) {
    if (bpm <= 0) return 0;
    // time_ms = (position / 192) * (60000 / bpm) * 4
    return static_cast<int64_t>((pos * 60000.0 * 4.0) / (MEASURE_LENGTH * bpm));
}

int PTParser::trackToLane(int trackIdx, int keyCount) {
    // trackIdx is the index in trackList (0-based)
    // 5K: trackList[2,3,4,5,6] -> lane 0,1,2,3,4
    // 7K: trackList[2,3,4,5,6,9,10] -> lane 0,1,2,3,4,5,6

    if (keyCount == 5) {
        if (trackIdx >= 2 && trackIdx <= 6) {
            return trackIdx - 2;
        }
    } else if (keyCount == 7) {
        if (trackIdx >= 2 && trackIdx <= 6) {
            return trackIdx - 2;  // lanes 0-4
        }
        if (trackIdx == 9) return 5;
        if (trackIdx == 10) return 6;
    }
    return -1;
}

bool PTParser::parse(const std::string& filepath, BeatmapInfo& info) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(fileSize);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();

    if (fileSize < 0x18) return false;

    // Verify magic
    if (buffer[0] != 'P' || buffer[1] != 'T' || buffer[2] != 'F' || buffer[3] != 'F') {
        return false;
    }

    // Detect key count from filename
    std::filesystem::path p(filepath);
    int keyCount = detectKeyCount(p.filename().string());

    // OGG entries map: ID -> filename
    std::unordered_map<uint8_t, std::string> oggMap;

    // Parse OGG entries (from 0x18 until first EZTR)
    size_t pos = 0x18;
    while (pos + 4 < fileSize) {
        if (buffer[pos] == 'E' && buffer[pos + 1] == 'Z' &&
            buffer[pos + 2] == 'T' && buffer[pos + 3] == 'R') {
            break;
        }

        if (pos + OGG_RECORD_LENGTH > fileSize) break;

        uint8_t id = buffer[pos];
        // uint8_t unknown = buffer[pos + 1];

        char filename[65] = {0};
        memcpy(filename, &buffer[pos + 2], 64);

        std::string fname = filename;
        size_t dotPos = fname.rfind('.');
        if (dotPos != std::string::npos) {
            std::string ext = fname.substr(dotPos);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".ogg") {
                fname = fname.substr(0, dotPos) + ".wav";
            }
        }
        oggMap[id] = fname;

        pos += OGG_RECORD_LENGTH;
    }

    // Now at first EZTR (BPM track)
    // Skip EZTR header (0x4e bytes)
    if (pos + EZTR_HEADER_LENGTH > fileSize) return false;
    pos += EZTR_HEADER_LENGTH;

    // Parse BPM events
    float baseBpm = 120.0f;  // Default BPM
    std::vector<PTBpmChange> bpmChanges;

    while (pos + 4 < fileSize) {
        if (buffer[pos] == 'E' && buffer[pos + 1] == 'Z' &&
            buffer[pos + 2] == 'T' && buffer[pos + 3] == 'R') {
            break;
        }

        if (pos + EVENT_RECORD_LENGTH > fileSize) break;

        // Event structure: Position(2) + pad(2) + Type(1) + BPM(4) + unknown(1) + pad(1)
        uint16_t position = buffer[pos] | (buffer[pos + 1] << 8);
        uint8_t type = buffer[pos + 4];

        if (type == PT_EVTT_TEMPO) {
            float bpm;
            memcpy(&bpm, &buffer[pos + 5], 4);

            if (position == 0) {
                baseBpm = bpm;
            }
            bpmChanges.push_back({position, bpm});
        }

        pos += EVENT_RECORD_LENGTH;
    }

    // If no BPM found, use default
    if (bpmChanges.empty()) {
        bpmChanges.push_back({0, baseBpm});
    }

    // Sort BPM changes
    std::sort(bpmChanges.begin(), bpmChanges.end(),
              [](const PTBpmChange& a, const PTBpmChange& b) {
                  return a.position < b.position;
              });

    // Parse note tracks
    std::vector<Note> notes;
    int trackIdx = 0;

    while (pos < fileSize) {
        if (buffer[pos] != 'E' || buffer[pos + 1] != 'Z' ||
            buffer[pos + 2] != 'T' || buffer[pos + 3] != 'R') {
            pos++;
            continue;
        }

        // Skip EZTR header
        if (pos + EZTR_HEADER_LENGTH > fileSize) break;
        pos += EZTR_HEADER_LENGTH;

        int lane = trackToLane(trackIdx, keyCount);

        // Parse events until next EZTR or end of file
        while (pos + 4 < fileSize) {
            if (buffer[pos] == 'E' && buffer[pos + 1] == 'Z' &&
                buffer[pos + 2] == 'T' && buffer[pos + 3] == 'R') {
                break;
            }

            if (pos + EVENT_RECORD_LENGTH > fileSize) break;

            // Event: Position(2) + pad(2) + Type(1) + ID(1) + Vol(1) + Pan(1) + unknown(1) + Length(2)
            uint16_t position = buffer[pos] | (buffer[pos + 1] << 8);
            uint8_t type = buffer[pos + 4];
            uint8_t soundId = buffer[pos + 5];
            uint8_t volume = buffer[pos + 6];
            // uint8_t pan = buffer[pos + 7];
            uint16_t length = buffer[pos + 9] | (buffer[pos + 10] << 8);

            if (type == PT_EVTT_NOTE) {
                // Find BPM at this position
                float bpm = baseBpm;
                for (const auto& bc : bpmChanges) {
                    if (bc.position <= position) {
                        bpm = bc.bpm;
                    } else {
                        break;
                    }
                }

                int64_t timeMs = positionToMs(position, bpm);

                auto it = oggMap.find(soundId);
                std::string soundFile = (it != oggMap.end()) ? it->second : "";

                if (lane >= 0) {
                    // Player note
                    bool isHold = (length > 6);
                    int64_t endTimeMs = isHold ? positionToMs(position + length, bpm) : 0;

                    Note note(lane, timeMs, isHold, endTimeMs);
                    note.volume = volume * 100 / 127;
                    note.filename = soundFile;
                    notes.push_back(note);
                } else {
                    // BGM/auto-play track - add as storyboard sample
                    if (!soundFile.empty()) {
                        StoryboardSample sample;
                        sample.time = timeMs;
                        sample.filename = soundFile;
                        sample.volume = volume * 100 / 127;
                        info.storyboardSamples.push_back(sample);
                    }
                }
            }

            pos += EVENT_RECORD_LENGTH;
        }

        trackIdx++;
    }

    // Sort notes
    std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        if (a.time != b.time) return a.time < b.time;
        return a.lane < b.lane;
    });

    // Fill BeatmapInfo
    info.notes = std::move(notes);
    info.keyCount = keyCount;
    info.mode = 3;

    std::string filename = p.stem().string();
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Extract tag from filename
    // Formats: kuda_ORG_MX_5KEY.pt, kuda_5k.pt, kuda_5kez2.pt, kuda_5knm7.pt
    std::string tag = lower;

    // Remove key count suffix (_5key, _7key, _5k, _7k) and trailing difficulty
    size_t keyPos = tag.rfind("_5key");
    if (keyPos == std::string::npos) keyPos = tag.rfind("_7key");
    if (keyPos != std::string::npos) {
        tag = tag.substr(0, keyPos);
    } else {
        // Try simplified format: _5k, _7k (possibly with difficulty suffix like ez2, nm7)
        keyPos = tag.find("_5k");
        if (keyPos == std::string::npos) keyPos = tag.find("_7k");
        if (keyPos != std::string::npos) {
            tag = tag.substr(0, keyPos);
        }
    }

    // Remove difficulty suffix (_hd, _nm, _ez, _mx, _sc)
    size_t diffPos = tag.rfind("_hd");
    if (diffPos == std::string::npos) diffPos = tag.rfind("_nm");
    if (diffPos == std::string::npos) diffPos = tag.rfind("_ez");
    if (diffPos == std::string::npos) diffPos = tag.rfind("_mx");
    if (diffPos == std::string::npos) diffPos = tag.rfind("_sc");
    if (diffPos != std::string::npos) tag = tag.substr(0, diffPos);

    // Remove _ORG suffix
    size_t orgPos = tag.rfind("_org");
    if (orgPos != std::string::npos) tag = tag.substr(0, orgPos);

    // Look up song in database
    bool found = false;
    for (int i = 0; i < DJMAX_SONG_COUNT; i++) {
        if (tag == DJMAX_SONG_DB[i].tag) {
            info.title = DJMAX_SONG_DB[i].title;
            info.artist = DJMAX_SONG_DB[i].composer;
            found = true;
            break;
        }
    }
    if (!found) {
        info.title = tag;
    }

    // Detect difficulty from original filename
    // Standard format: kuda_ORG_MX_5KEY.pt -> MX
    // Simplified format: fire_5kez2.pt, fire_5knm5.pt, fire_5kMX.pt
    // No suffix (e.g., fire_5k.pt) = Hard difficulty
    info.version = "Hard";  // Default (no suffix = Hard)

    // First try standard format with underscore separators
    if (lower.find("_hd_") != std::string::npos || lower.rfind("_hd") == lower.length() - 3)
        info.version = "Hard";
    else if (lower.find("_mx_") != std::string::npos || lower.rfind("_mx") == lower.length() - 3)
        info.version = "Maximum";
    else if (lower.find("_sc_") != std::string::npos || lower.rfind("_sc") == lower.length() - 3)
        info.version = "SC";
    else if (lower.find("_nm_") != std::string::npos || lower.rfind("_nm") == lower.length() - 3)
        info.version = "Normal";
    else if (lower.find("_ez_") != std::string::npos || lower.rfind("_ez") == lower.length() - 3)
        info.version = "Easy";
    else {
        // Try simplified format: extract difficulty after _5k or _7k
        size_t kpos = lower.find("_5k");
        if (kpos == std::string::npos) kpos = lower.find("_7k");
        if (kpos != std::string::npos && kpos + 3 < lower.length()) {
            std::string suffix = lower.substr(kpos + 3);  // e.g., "ez2", "nm5", "mx"
            if (suffix.find("hd") == 0)
                info.version = "Hard";
            else if (suffix.find("mx") == 0)
                info.version = "Maximum";
            else if (suffix.find("sc") == 0)
                info.version = "SC";
            else if (suffix.find("nm") == 0)
                info.version = "Normal";
            else if (suffix.find("ez") == 0)
                info.version = "Easy";
        }
    }

    info.od = 8.0f;
    info.hp = 5.0f;
    info.creator = "Pentavision";
    info.totalObjectCount = static_cast<int>(info.notes.size());

    std::cout << "Loaded PT chart: " << keyCount << "K, " << info.notes.size()
              << " notes, BPM=" << baseBpm << std::endl;

    return true;
}
