#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "Note.h"
#include "OsuParser.h"

// O2Jam difficulty levels
enum class OjnDifficulty {
    Easy = 0,
    Normal = 1,
    Hard = 2
};

// O2Jam genre types
enum class OjnGenre {
    Ballad = 0,
    Rock = 1,
    Dance = 2,
    Techno = 3,
    HipHop = 4,
    SoulRnB = 5,
    Jazz = 6,
    Funk = 7,
    Classical = 8,
    Traditional = 9,
    Etc = 10
};

// OJN file header (300 bytes)
struct OjnHeader {
    int32_t songId;
    char signature[4];      // "ojn\0"
    float encodeVersion;
    int32_t genre;
    float bpm;
    int16_t level[4];       // Easy, Normal, Hard, (unused)
    int32_t eventCount[3];
    int32_t noteCount[3];
    int32_t measureCount[3];
    int32_t packageCount[3];
    int16_t oldEncode;
    int16_t oldSongId;
    char oldGenre[20];
    int32_t bmpSize;
    int32_t oldFileVersion;
    char title[64];
    char artist[32];
    char noter[32];
    char ojmFile[32];
    int32_t coverSize;
    int32_t time[3];        // Duration in seconds for each difficulty
    int32_t noteOffset[3];  // Offset to note data for each difficulty
    int32_t coverOffset;    // Offset to cover image
};

// OJN note event (4 bytes per event in package)
struct OjnEvent {
    int16_t sampleId;   // Sample/keysound ID (0 = no sound)
    int8_t pan;         // Panning (-127 to 127)
    int8_t noteType;    // 0=normal, 2=hold start, 3=hold end
};

class OjnParser {
public:
    // Parse .ojn file and fill BeatmapInfo
    static bool parse(const std::string& filepath, BeatmapInfo& info,
                      OjnDifficulty difficulty = OjnDifficulty::Hard);

    // Check if file is an OJN file
    static bool isOjnFile(const std::string& filepath);

    // Get available difficulties in the file
    static std::vector<OjnDifficulty> getAvailableDifficulties(const std::string& filepath);

    // Get OJN header info
    static bool getHeader(const std::string& filepath, OjnHeader& header);

private:
    // Convert measure position to milliseconds
    static int64_t measureToMs(int measure, int position, float bpm,
                               const std::vector<std::pair<int, float>>& bpmChanges);

    // Read null-terminated string from buffer
    static std::string readString(const char* buffer, size_t maxLen);
};
