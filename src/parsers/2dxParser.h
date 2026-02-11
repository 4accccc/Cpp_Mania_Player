#pragma once
#include "OsuParser.h"
#include <string>
#include <vector>
#include <cstdint>

// beatmania IIDX .1 chart file parser
class IIDXParser {
public:
    // IIDX difficulty indices
    enum Difficulty {
        SP_BEGINNER = 1,
        SP_NORMAL = 2,
        SP_HYPER = 0,
        SP_ANOTHER = 3,
        SP_LEGGENDARIA = 4,
        DP_NORMAL = 6,
        DP_HYPER = 5,
        DP_ANOTHER = 7,
        DP_LEGGENDARIA = 8
    };

    // Parse IIDX .1 chart file
    // difficulty: 0-9 (see Difficulty enum)
    static bool parse(const std::string& path, BeatmapInfo& info, int difficulty = SP_ANOTHER);

    // Get difficulty name
    static const char* getDifficultyName(int difficulty);

    // Check if difficulty exists in file
    static bool hasDifficulty(const std::string& path, int difficulty);

    // Get available difficulties
    static std::vector<int> getAvailableDifficulties(const std::string& path);

private:
    // Event types (verified from iidx2osu reference)
    enum EventType {
        EVENT_NOTE_KEY = 0x00,      // Key note (1-7), value = duration for LN
        EVENT_NOTE_SCRATCH = 0x01, // Scratch note, value = duration for LN
        EVENT_SAMPLE_KEY = 0x02,   // Keysound for key lanes
        EVENT_SAMPLE_SCRATCH = 0x03, // Keysound for scratch
        EVENT_BPM = 0x04,
        EVENT_TIMESIG = 0x05,
        EVENT_END = 0x06,
        EVENT_BGM = 0x07,
        EVENT_PARAM = 0x08,
        EVENT_MEASURE = 0x0C,
        EVENT_HEADER = 0x10
    };

    // Difficulty to file offset mapping
    static int getDifficultyOffset(int difficulty);

    // Parse events from chart data
    static bool parseEvents(const uint8_t* data, size_t size, BeatmapInfo& info);
};
