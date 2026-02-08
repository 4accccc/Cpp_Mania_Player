#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "Note.h"
#include "OsuParser.h"

// DJMAX key mode
enum class DJMaxKeyMode {
    Key4B = 4,
    Key5B = 5,
    Key6B = 6,
    Key8B = 8
};

// DJMAX instrument (audio sample) info
struct DJMaxInstrument {
    uint16_t insNo;
    std::string filename;
};

// DJMAX chart event
struct DJMaxEvent {
    uint32_t tick;
    uint8_t type;       // 1=note, 2=volume, 3=tempo
    uint16_t insNo;     // audio index
    uint8_t velocity;
    uint8_t pan;
    uint8_t attribute;
    uint16_t duration;  // for hold notes
    float tempo;        // for tempo events
    uint8_t volume;     // for volume events
};

// DJMAX track data
struct DJMaxTrack {
    std::vector<DJMaxEvent> events;
};

// DJMAX chart header
struct DJMaxHeader {
    float tempo;
    uint16_t tpm;           // ticks per measure
    uint16_t insCount;
    uint32_t totalTick;
    float playTime;
    uint32_t endTick;
    uint8_t trackCount;
};

class DJMaxParser {
public:
    // Parse .bytes file and fill BeatmapInfo
    static bool parse(const std::string& filepath, BeatmapInfo& info);

    // Detect key mode from filename (e.g., "song_4b_nm.bytes" -> 4B)
    static DJMaxKeyMode detectKeyMode(const std::string& filename);

    // Check if file is a DJMAX chart
    static bool isDJMaxChart(const std::string& filepath);

private:
    // Convert DJMAX track index to lane number
    // Returns -1 if track should be excluded (analog, side keys)
    static int trackToLane(int trackIdx, DJMaxKeyMode keyMode);

    // Convert tick to milliseconds
    static int64_t tickToMs(uint32_t tick, float tps);
};
