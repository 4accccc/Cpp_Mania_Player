#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "Note.h"
#include "OsuParser.h"

struct BMSChannel {
    int measure;
    int channel;
    std::vector<std::string> data;  // Base36 pairs
};

// BMS BGA event
struct BMSBgaEvent {
    int64_t time;       // Milliseconds
    int layer;          // 0=BGA, 1=Layer, 2=Poor
    int bmpId;          // BMP definition ID
    std::string filename;
};

// BMS parse result (includes BGA info)
struct BMSData {
    BeatmapInfo beatmap;
    std::vector<BMSBgaEvent> bgaEvents;
    std::unordered_map<int, std::string> wavDefs;  // WAV definitions
    std::unordered_map<int, std::string> bmpDefs;  // BMP definitions
    std::string directory;  // BMS file directory
};

class BMSParser {
public:
    static bool parse(const std::string& filepath, BeatmapInfo& info);
    static bool parseFull(const std::string& filepath, BMSData& data);
    static bool isBMSFile(const std::string& filepath);

private:
    static int base36ToInt(const std::string& str);
    static std::string intToBase36(int value);
    static std::string trim(const std::string& str);
    static int channelToLane(int channel, int keyCount);
    static int detectKeyCount(const std::vector<BMSChannel>& channels);
};
