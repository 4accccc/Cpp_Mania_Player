#pragma once
#include "OsuParser.h"
#include <string>
#include <vector>
#include <unordered_map>

// MUSYNX chart parser
// Time unit: 0.1 microseconds (100 nanoseconds), divide by 10000 to get milliseconds
class MuSynxParser {
public:
    // Parse a MUSYNX chart file (.txt)
    // Returns true on success, fills info with parsed data
    static bool parse(const std::string& path, BeatmapInfo& info);

    // Get key count from filename (e.g., "song4T_easy.txt" -> 4)
    static int getKeyCountFromFilename(const std::string& filename);

    // Extract cue names from chart file in WAV definition order
    // Returns list of cue names (e.g., ["BGM", "kick_000", "kick_001", ...])
    static std::vector<std::string> extractCueNames(const std::string& path);

private:
    // Convert MUSYNX time (0.1 microseconds) to milliseconds
    static int64_t timeToMs(int64_t musynxTime);

    // Convert track number to lane index based on key count
    // 4K: tracks 3,4,6,7 -> lanes 0,1,2,3
    // 6K: tracks 2-7 -> lanes 0-5
    static int trackToLane(int track, int keyCount);

    // Parse WAV ID (base-36 two-character ID like "0B", "1Z")
    static std::string parseWavId(const std::string& id);
};
