#include "DJMaxParser.h"
#include <fstream>
#include <cstring>
#include <cmath>
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
    // Exclude L2/R2 side keys (always)
    if (trackIdx == TRACK_L2 || trackIdx == TRACK_R2)
        return -1;

    switch (keyMode) {
        case DJMaxKeyMode::Key4B:
            // ANALOG_L, 3,4,5,6, ANALOG_R -> lane 0-5 (6K)
            if (trackIdx == TRACK_ANALOG_L) return 0;
            if (trackIdx >= 3 && trackIdx <= 6) return trackIdx - 2;
            if (trackIdx == TRACK_ANALOG_R) return 5;
            return -1;

        case DJMaxKeyMode::Key5B:
            // ANALOG_L, 3,4,5,6,7, ANALOG_R -> lane 0-6 (7K)
            if (trackIdx == TRACK_ANALOG_L) return 0;
            if (trackIdx >= 3 && trackIdx <= 7) return trackIdx - 2;
            if (trackIdx == TRACK_ANALOG_R) return 6;
            return -1;

        case DJMaxKeyMode::Key6B:
            // ANALOG_L, 3,4,5,6,7,8, ANALOG_R -> lane 0-7 (8K)
            if (trackIdx == TRACK_ANALOG_L) return 0;
            if (trackIdx >= 3 && trackIdx <= 8) return trackIdx - 2;
            if (trackIdx == TRACK_ANALOG_R) return 7;
            return -1;

        case DJMaxKeyMode::Key8B:
            // ANALOG_L, L1, 3-8, R1, ANALOG_R -> lane 0-9 (10K)
            if (trackIdx == TRACK_ANALOG_L) return 0;
            if (trackIdx == TRACK_L1) return 1;
            if (trackIdx >= 3 && trackIdx <= 8) return trackIdx - 1;
            if (trackIdx == TRACK_R1) return 8;
            if (trackIdx == TRACK_ANALOG_R) return 9;
            return -1;
    }
    return -1;
}

int64_t DJMaxParser::tickToMs(uint32_t tick, float tps) {
    if (tps <= 0) return 0;
    return static_cast<int64_t>((tick / tps) * 1000.0f);
}

double DJMaxParser::tickToMsVar(uint32_t tick, const std::vector<TempoEvent>& timeline,
                                 uint16_t tpm) {
    if (timeline.empty() || tpm == 0) return 0.0;

    // Find the active tempo segment for this tick
    const TempoEvent* active = &timeline[0];
    for (size_t i = 1; i < timeline.size(); i++) {
        if (timeline[i].tick <= tick)
            active = &timeline[i];
        else
            break;
    }

    // ms_per_tick = 240000 / (BPM * tpm), assuming 4/4 time
    double msPerTick = 240000.0 / (active->bpm * tpm);
    return active->timeMs + (tick - active->tick) * msPerTick;
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
    // 4B/5B/6B include analog tracks (+2), 8B includes analog+L1/R1 (+2)
    int keyCount = 0;
    switch (keyMode) {
        case DJMaxKeyMode::Key4B: keyCount = 6; break;
        case DJMaxKeyMode::Key5B: keyCount = 7; break;
        case DJMaxKeyMode::Key6B: keyCount = 8; break;
        case DJMaxKeyMode::Key8B: keyCount = 10; break;
    }

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

        std::string filename = name;
        instruments[i].filename = filename;
        insNoToFilename[instruments[i].insNo] = filename;
    }

    // Find BGM instrument number (filename starting with "0-" or "_", not blank)
    uint16_t bgmInsNo = 0;
    bool bgmInsFound = false;
    for (const auto& ins : instruments) {
        std::string lf = ins.filename;
        std::transform(lf.begin(), lf.end(), lf.begin(), ::tolower);
        if (lf.find("blank") != std::string::npos) continue;

        bool isBgm = false;
        // Pattern 1: "0-filename.wav" (common in most charts)
        if (ins.filename.length() >= 2 && ins.filename[0] == '0' && ins.filename[1] == '-')
            isBgm = true;
        // Pattern 2: "_filename.wav" with "bgm" in name (e.g. "_die in bgm_04.wav")
        if (!isBgm && ins.filename.length() >= 2 && ins.filename[0] == '_' &&
            lf.find("bgm") != std::string::npos)
            isBgm = true;

        if (isBgm) {
            bgmInsNo = ins.insNo;
            bgmInsFound = true;
            break;
        }
    }

    // Read tracks and events (two-pass: collect tempo events first, then notes)
    std::vector<Note> notes;
    uint32_t bgmTriggerTick = UINT32_MAX;  // earliest tick where BGM keysound is triggered

    // First pass: collect tempo events to build timeline
    std::vector<TempoEvent> tempoTimeline;
    {
        size_t savedPos = pos;
        for (uint8_t trackIdx = 0; trackIdx < trackCount; trackIdx++) {
            pos += 2;  // skip 2 bytes
            pos += 64; // skip 64 bytes (track name)
            pos += 4;  // skip 4 bytes
            uint32_t eventCount = readU32();

            for (uint32_t e = 0; e < eventCount; e++) {
                uint32_t tick = readU32();
                uint8_t evtType = readByte();
                pos += 8; // skip event data

                if (evtType == PT_EVTT_TEMPO) {
                    float newTempo;
                    memcpy(&newTempo, &buffer[pos - 8], 4);
                    if (newTempo >= 1.0f && newTempo < 10000.0f) {
                        tempoTimeline.push_back({tick, newTempo, 0.0});
                    }
                }
            }
        }
        pos = savedPos; // reset for second pass
    }

    // Build tempo timeline with accumulated times
    bool hasTempoChanges = !tempoTimeline.empty();
    if (hasTempoChanges) {
        // Add initial BPM at tick 0 if no event at tick 0
        if (tempoTimeline.empty() || tempoTimeline[0].tick > 0) {
            tempoTimeline.insert(tempoTimeline.begin(), {0, tempo, 0.0});
        }
        std::sort(tempoTimeline.begin(), tempoTimeline.end(),
                  [](const TempoEvent& a, const TempoEvent& b) { return a.tick < b.tick; });
        // Remove duplicates at same tick (keep last)
        for (int i = (int)tempoTimeline.size() - 2; i >= 0; i--) {
            if (tempoTimeline[i].tick == tempoTimeline[i + 1].tick)
                tempoTimeline.erase(tempoTimeline.begin() + i);
        }
        // Compute accumulated time: ms_per_tick = 240000 / (BPM * tpm), assuming 4/4
        tempoTimeline[0].timeMs = 0.0;
        for (size_t i = 1; i < tempoTimeline.size(); i++) {
            double msPerTick = 240000.0 / (tempoTimeline[i - 1].bpm * tpm);
            uint32_t deltaTick = tempoTimeline[i].tick - tempoTimeline[i - 1].tick;
            tempoTimeline[i].timeMs = tempoTimeline[i - 1].timeMs + deltaTick * msPerTick;
        }
    }

    // Second pass: parse notes using tempo-aware conversion
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

            if (evtType == PT_EVTT_NOTE) {
                uint16_t insNo = data[0] | (data[1] << 8);

                // Track BGM trigger tick (on any track)
                if (bgmInsFound && insNo == bgmInsNo && tick < bgmTriggerTick) {
                    bgmTriggerTick = tick;
                }

                // Only create playable notes on valid lanes
                if (lane >= 0) {
                    uint8_t velocity = data[2];
                    uint8_t pan = data[3];
                    uint8_t attribute = data[4];
                    uint16_t duration = data[5] | (data[6] << 8);
                    bool isHold = (duration > 6);

                    int64_t timeMs, endTimeMs = 0;
                    if (hasTempoChanges) {
                        timeMs = (int64_t)std::round(tickToMsVar(tick, tempoTimeline, tpm));
                        if (isHold)
                            endTimeMs = (int64_t)std::round(tickToMsVar(tick + duration, tempoTimeline, tpm));
                    } else {
                        timeMs = tickToMs(tick, tps);
                        if (isHold)
                            endTimeMs = tickToMs(tick + duration, tps);
                    }

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
    }

    // Sort notes by time
    std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        if (a.time != b.time) return a.time < b.time;
        return a.lane < b.lane;
    });

    // Apply BGM trigger offset for PC version charts
    // In DJMAX, BGM is a keysound triggered at a specific tick, not played from time 0.
    // Since we play BGM from time 0, shift all notes earlier by the BGM trigger time.
    // Only apply to PC version (detected by .wav extensions; PS4 uses .ogg).
    bool isPCVersion = false;
    for (const auto& ins : instruments) {
        if (ins.filename.size() > 4) {
            std::string ext = ins.filename.substr(ins.filename.size() - 4);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".wav") { isPCVersion = true; break; }
            if (ext == ".ogg") { break; }
        }
    }
    if (isPCVersion && bgmTriggerTick != UINT32_MAX) {
        int64_t bgmOffsetMs;
        if (hasTempoChanges) {
            bgmOffsetMs = -(int64_t)std::round(tickToMsVar(bgmTriggerTick, tempoTimeline, tpm));
        } else {
            bgmOffsetMs = -tickToMs(bgmTriggerTick, tps);
        }
        for (auto& n : notes) {
            n.time += bgmOffsetMs;
            if (n.isHold) n.endTime += bgmOffsetMs;
        }
    }

    // Fill BeatmapInfo
    info.notes = std::move(notes);
    info.keyCount = keyCount;
    info.mode = 3; // mania mode

    // Generate timing points from tempo timeline for SV rendering
    if (hasTempoChanges && tempoTimeline.size() > 0) {
        // Compute the BGM offset used above (to shift timing points too)
        int64_t bgmOffsetMs = 0;
        if (isPCVersion && bgmTriggerTick != UINT32_MAX) {
            bgmOffsetMs = -(int64_t)std::round(tickToMsVar(bgmTriggerTick, tempoTimeline, tpm));
        }
        for (const auto& te : tempoTimeline) {
            TimingPoint tp;
            tp.time = std::round(te.timeMs) + bgmOffsetMs;
            tp.beatLength = 60000.0 / te.bpm;  // ms per beat
            tp.uninherited = true;              // red line (BPM change)
            tp.effectiveBeatLength = tp.beatLength;
            tp.volume = 100;
            info.timingPoints.push_back(tp);
        }
    } else {
        // Single timing point for constant BPM
        TimingPoint tp;
        tp.time = 0;
        tp.beatLength = 60000.0 / tempo;
        tp.uninherited = true;
        tp.effectiveBeatLength = tp.beatLength;
        tp.volume = 100;
        info.timingPoints.push_back(tp);
    }

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

    // Find background music - "0-" prefix or "_" prefix with "bgm" in name
    for (const auto& ins : instruments) {
        std::string lower = ins.filename;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find("blank") != std::string::npos) continue;

        bool isBgm = false;
        if (ins.filename.length() >= 2 && ins.filename[0] == '0' && ins.filename[1] == '-')
            isBgm = true;
        if (!isBgm && ins.filename.length() >= 2 && ins.filename[0] == '_' &&
            lower.find("bgm") != std::string::npos)
            isBgm = true;

        if (isBgm) {
            info.audioFilename = ins.filename;
            break;
        }
    }

    return true;
}
