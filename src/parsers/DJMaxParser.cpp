#include "DJMaxParser.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <unordered_map>

// DJMAX track index constants
namespace {
    constexpr int TRACK_ANALOG_L = 2;
    constexpr int TRACK_ANALOG_R = 9;
    constexpr int TRACK_L1 = 10;
    constexpr int TRACK_R1 = 11;
    constexpr int TRACK_L2 = 12;
    constexpr int TRACK_R2 = 13;
    constexpr int TRACK_FIRST_KEY = 3;

    // Event types
    constexpr uint8_t PT_EVTT_NOTE = 1;
    constexpr uint8_t PT_EVTT_VOLUME = 2;
    constexpr uint8_t PT_EVTT_TEMPO = 3;
    constexpr uint8_t PT_EVTT_BEAT = 4;
}

bool DJMaxParser::isDJMaxChart(const std::string& filepath) {
    std::filesystem::path p(filepath);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext != ".bytes") return false;

    std::string filename = p.stem().string();
    std::transform(filename.begin(), filename.end(), filename.begin(), ::tolower);

    // Check for pattern like "song_4b_nm" or "song_5b_hd"
    return filename.find("_4b_") != std::string::npos ||
           filename.find("_5b_") != std::string::npos ||
           filename.find("_6b_") != std::string::npos ||
           filename.find("_8b_") != std::string::npos;
}

DJMaxKeyMode DJMaxParser::detectKeyMode(const std::string& filename) {
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.find("_8b_") != std::string::npos || lower.find("8b.") != std::string::npos)
        return DJMaxKeyMode::Key8B;
    if (lower.find("_6b_") != std::string::npos || lower.find("6b.") != std::string::npos)
        return DJMaxKeyMode::Key6B;
    if (lower.find("_5b_") != std::string::npos || lower.find("5b.") != std::string::npos)
        return DJMaxKeyMode::Key5B;
    return DJMaxKeyMode::Key4B;
}

int DJMaxParser::trackToLane(int trackIdx, DJMaxKeyMode keyMode) {
    // Exclude analog tracks
    if (trackIdx == TRACK_ANALOG_L || trackIdx == TRACK_ANALOG_R)
        return -1;

    // Exclude L2/R2 side keys (always)
    if (trackIdx == TRACK_L2 || trackIdx == TRACK_R2)
        return -1;

    int keyCount = static_cast<int>(keyMode);

    switch (keyMode) {
        case DJMaxKeyMode::Key4B:
            // Track 3,4,5,6 -> lane 0,1,2,3
            if (trackIdx >= 3 && trackIdx <= 6)
                return trackIdx - 3;
            return -1;

        case DJMaxKeyMode::Key5B:
            // Track 3,4,5,6,7 -> lane 0,1,2,3,4
            if (trackIdx >= 3 && trackIdx <= 7)
                return trackIdx - 3;
            return -1;

        case DJMaxKeyMode::Key6B:
            // Track 3,4,5,6,7,8 -> lane 0,1,2,3,4,5
            if (trackIdx >= 3 && trackIdx <= 8)
                return trackIdx - 3;
            return -1;

        case DJMaxKeyMode::Key8B:
            // L1(10), Track 3-8, R1(11) -> lane 0,1,2,3,4,5,6,7
            if (trackIdx == TRACK_L1) return 0;
            if (trackIdx >= 3 && trackIdx <= 8) return trackIdx - 2;
            if (trackIdx == TRACK_R1) return 7;
            return -1;
    }
    return -1;
}

int64_t DJMaxParser::tickToMs(uint32_t tick, float tps) {
    if (tps <= 0) return 0;
    return static_cast<int64_t>((tick / tps) * 1000.0f);
}

bool DJMaxParser::parse(const std::string& filepath, BeatmapInfo& info) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    // Read entire file
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(fileSize);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();

    if (fileSize < 8) return false;

    // Parse file header
    size_t pos = 0;
    auto readU16 = [&]() -> uint16_t {
        uint16_t v = buffer[pos] | (buffer[pos + 1] << 8);
        pos += 2;
        return v;
    };
    auto readU32 = [&]() -> uint32_t {
        uint32_t v = buffer[pos] | (buffer[pos + 1] << 8) |
                     (buffer[pos + 2] << 16) | (buffer[pos + 3] << 24);
        pos += 4;
        return v;
    };
    auto readFloat = [&]() -> float {
        float v;
        memcpy(&v, &buffer[pos], 4);
        pos += 4;
        return v;
    };
    auto readByte = [&]() -> uint8_t {
        return buffer[pos++];
    };

    // File header
    uint32_t verAndSig = readU32();
    uint32_t headerOffset = readU32();

    // Save instrument list position
    size_t insListPos = pos;

    // Read header data
    pos = headerOffset;
    uint16_t insCount = readU16();
    uint8_t trackCount = static_cast<uint8_t>(readU16());
    uint16_t tpm = readU16();
    float tempo = readFloat();
    uint32_t totalTick = readU32();
    float playTime = readFloat();
    uint32_t endTick = readU32();
    readU32(); // unknown

    // Calculate TPS (ticks per second)
    float tps = (playTime > 0) ? (totalTick / playTime) : 192.0f;

    // Detect key mode from filename
    std::filesystem::path p(filepath);
    DJMaxKeyMode keyMode = detectKeyMode(p.filename().string());
    int keyCount = static_cast<int>(keyMode);

    // Read instrument list (for audio filenames)
    pos = insListPos;
    std::vector<DJMaxInstrument> instruments(insCount);
    // Map insNo -> filename for quick lookup
    std::unordered_map<uint16_t, std::string> insNoToFilename;
    for (uint16_t i = 0; i < insCount; i++) {
        instruments[i].insNo = readU16();
        readByte(); // unknown byte
        char name[65] = {0};
        memcpy(name, &buffer[pos], 64);
        pos += 64;

        // Convert .ogg to .wav (user's packed format)
        std::string filename = name;
        size_t dotPos = filename.rfind('.');
        if (dotPos != std::string::npos) {
            std::string ext = filename.substr(dotPos);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".ogg") {
                filename = filename.substr(0, dotPos) + ".wav";
            }
        }
        instruments[i].filename = filename;
        insNoToFilename[instruments[i].insNo] = filename;
    }

    // Read tracks and events
    std::vector<Note> notes;
    for (uint8_t trackIdx = 0; trackIdx < trackCount; trackIdx++) {
        pos += 2;  // skip 2 bytes
        pos += 64; // skip 64 bytes (track name)
        pos += 4;  // skip 4 bytes
        uint32_t eventCount = readU32();

        int lane = trackToLane(trackIdx, keyMode);

        for (uint32_t e = 0; e < eventCount; e++) {
            uint32_t tick = readU32();
            uint8_t evtType = readByte();
            uint8_t data[8];
            memcpy(data, &buffer[pos], 8);
            pos += 8;

            // Only process note events on valid lanes
            if (evtType == PT_EVTT_NOTE && lane >= 0) {
                uint16_t insNo = data[0] | (data[1] << 8);
                uint8_t velocity = data[2];
                uint8_t pan = data[3];
                uint8_t attribute = data[4];
                uint16_t duration = data[5] | (data[6] << 8);

                int64_t timeMs = tickToMs(tick, tps);
                bool isHold = (duration > 6);
                int64_t endTimeMs = isHold ? tickToMs(tick + duration, tps) : 0;

                Note note(lane, timeMs, isHold, endTimeMs);
                note.volume = velocity * 100 / 127;

                // Set key sound filename based on insNo
                auto it = insNoToFilename.find(insNo);
                if (it != insNoToFilename.end()) {
                    note.filename = it->second;
                }

                notes.push_back(note);
            }
        }
    }

    // Sort notes by time
    std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        if (a.time != b.time) return a.time < b.time;
        return a.lane < b.lane;
    });

    // Fill BeatmapInfo
    info.notes = std::move(notes);
    info.keyCount = keyCount;
    info.mode = 3; // mania mode

    // Extract song name from filepath
    std::string filename = p.stem().string();
    size_t underscorePos = filename.find('_');
    if (underscorePos != std::string::npos) {
        info.title = filename.substr(0, underscorePos);
    } else {
        info.title = filename;
    }

    // Set difficulty name based on filename
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.find("_sc") != std::string::npos) info.version = "SC";
    else if (lower.find("_mx") != std::string::npos) info.version = "MX";
    else if (lower.find("_hd") != std::string::npos) info.version = "HD";
    else if (lower.find("_nm") != std::string::npos) info.version = "NM";
    else info.version = "Unknown";

    info.od = 8.0f;
    info.hp = 5.0f;
    info.totalObjectCount = static_cast<int>(info.notes.size());

    // Find background music - files starting with "0-" are background music
    for (const auto& ins : instruments) {
        if (ins.filename.length() >= 2 && ins.filename[0] == '0' && ins.filename[1] == '-') {
            // Skip "0-blank" which is silence
            std::string lower = ins.filename;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find("blank") == std::string::npos) {
                info.audioFilename = ins.filename;
                break;
            }
        }
    }

    return true;
}
