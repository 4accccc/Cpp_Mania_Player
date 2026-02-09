#pragma once
#include "OsuParser.h"
#include <string>

// Malody game modes
enum class MalodyMode {
    Key = 0,      // mania-style
    Step = 1,     // DDR-style (not supported)
    DJ = 2,       // DJ-style (not supported)
    Catch = 3,    // catch-style (not supported)
    Pad = 4,      // pad-style (not supported)
    Taiko = 5,    // taiko-style (not supported)
    Ring = 6,     // ring-style (not supported)
    Slide = 7     // slide-style (not supported)
};

class MalodyParser {
public:
    // Parse .mc file and fill BeatmapInfo
    static bool parse(const std::string& path, BeatmapInfo& info);

private:
    // Convert beat array [measure, numerator, denominator] to milliseconds
    static int64_t beatToMs(int measure, int numerator, int denominator,
                           double bpm, const std::vector<std::pair<double, double>>& bpmChanges);

    // Find effective BPM at given beat position
    static double getBpmAtBeat(double beatPos, double baseBpm,
                               const std::vector<std::pair<double, double>>& bpmChanges);
};
