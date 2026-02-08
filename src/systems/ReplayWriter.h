#pragma once
#include <string>
#include <vector>
#include "ReplayParser.h"

class ReplayWriter {
public:
    static bool write(const std::string& filepath,
                      const std::string& beatmapHash,
                      const std::string& playerName,
                      int keyCount,
                      const int* judgeCounts,
                      int maxCombo,
                      int score,
                      int mods,
                      const std::vector<ReplayFrame>& frames);

private:
    static void writeByte(std::vector<uint8_t>& data, uint8_t value);
    static void writeInt16(std::vector<uint8_t>& data, int16_t value);
    static void writeInt32(std::vector<uint8_t>& data, int32_t value);
    static void writeInt64(std::vector<uint8_t>& data, int64_t value);
    static void writeOsuString(std::vector<uint8_t>& data, const std::string& str);
    static bool compressLZMA(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);
};
