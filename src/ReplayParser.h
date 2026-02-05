#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct ReplayFrame {
    int64_t time;       // Absolute time (ms)
    int keyState;       // Key state bitmask
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
    std::vector<ReplayFrame> frames;
};

class ReplayParser {
public:
    static bool parse(const std::string& filepath, ReplayInfo& info);
    static std::string getBeatmapHash(const std::string& filepath);

private:
    static std::string readOsuString(const uint8_t* data, size_t& offset);
    static int32_t readInt32(const uint8_t* data, size_t& offset);
    static int16_t readInt16(const uint8_t* data, size_t& offset);
    static int64_t readInt64(const uint8_t* data, size_t& offset);
    static uint8_t readByte(const uint8_t* data, size_t& offset);
    static uint64_t readULEB128(const uint8_t* data, size_t& offset);
    static bool decompressLZMA(const uint8_t* compressed, size_t compressedSize,
                               std::vector<uint8_t>& decompressed);
    static void parseFrames(const std::string& data, std::vector<ReplayFrame>& frames);
};
