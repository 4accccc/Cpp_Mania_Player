#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "Note.h"
#include "OsuParser.h"

// PT file BPM change event
struct PTBpmChange {
    uint16_t position;
    float bpm;
};

class PTParser {
public:
    static bool parse(const std::string& filepath, BeatmapInfo& info);
    static bool isPTFile(const std::string& filepath);

private:
    static int detectKeyCount(const std::string& filename);
    static int64_t positionToMs(uint16_t pos, float bpm);
    static int trackToLane(int trackIdx, int keyCount);
    static std::string extractTag(const std::string& filename);
};
