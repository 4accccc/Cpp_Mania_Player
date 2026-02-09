#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct ReplayFrame {
    int64_t time;       // Absolute time (ms)
    int keyState;       // Key state bitmask
    float x;            // X position (for std/taiko/ctb)
    float y;            // Y position
};

struct ReplayInfo {
    int gameMode;
    int gameVersion;
    std::string beatmapHash;
    std::string playerName;
    std::string replayHash;
    int count300g;
    int count300;
    int count200;
    int count100;
    int count50;
    int countMiss;
    int totalScore;
    int maxCombo;
    bool perfectCombo;
    int mods;
    int64_t timestamp;
    int seed;  // Random seed for replay
    int64_t onlineScoreId;  // Online score ID from end marker
    double targetAccuracy;  // Target mode: accuracy * totalNotes (only if Target mod enabled)
    std::vector<ReplayFrame> frames;
};

class ReplayParser {
public:
    static bool parse(const std::string& filepath, ReplayInfo& info);
    static bool save(const std::string& filepath, const ReplayInfo& info);
    static std::string getBeatmapHash(const std::string& filepath);
    static std::string calculateReplayHash(const ReplayInfo& info);
    static void mirrorKeys(ReplayInfo& info, int keyCount);
    static int detectKeyCount(const ReplayInfo& info);  // Detect key count from replay data

    // Watermark functions
    static int64_t createWatermark();  // Create watermark with current time
    static bool hasWatermark(int64_t onlineScoreId);  // Check if watermark exists
    static int64_t getWatermarkTime(int64_t onlineScoreId);  // Extract timestamp from watermark (ms)

private:
    // Read methods
    static std::string readOsuString(const uint8_t* data, size_t& offset);
    static int32_t readInt32(const uint8_t* data, size_t& offset);
    static int16_t readInt16(const uint8_t* data, size_t& offset);
    static int64_t readInt64(const uint8_t* data, size_t& offset);
    static uint8_t readByte(const uint8_t* data, size_t& offset);
    static uint64_t readULEB128(const uint8_t* data, size_t& offset);
    static bool decompressLZMA(const uint8_t* compressed, size_t compressedSize,
                               std::vector<uint8_t>& decompressed);
    static void parseFrames(const std::string& data, std::vector<ReplayFrame>& frames, int& seedOut, int64_t& scoreIdOut);

    // Write methods
    static void writeByte(std::vector<uint8_t>& buffer, uint8_t value);
    static void writeInt16(std::vector<uint8_t>& buffer, int16_t value);
    static void writeInt32(std::vector<uint8_t>& buffer, int32_t value);
    static void writeInt64(std::vector<uint8_t>& buffer, int64_t value);
    static void writeULEB128(std::vector<uint8_t>& buffer, uint64_t value);
    static void writeOsuString(std::vector<uint8_t>& buffer, const std::string& str);
    static bool compressLZMA(const std::vector<uint8_t>& data, std::vector<uint8_t>& compressed);
    static std::string serializeFrames(const std::vector<ReplayFrame>& frames, int seed, int64_t scoreId);
};
