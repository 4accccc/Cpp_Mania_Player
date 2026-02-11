#pragma once
#include <string>
#include <vector>
#include <cstdint>

class TwoDxParser {
public:
    struct Sample {
        uint32_t offset;      // File offset
        uint32_t headerSize;  // Sound header size
        int32_t waveSize;     // Wave data size
        uint32_t waveOffset;  // Wave data offset (offset + headerSize)
    };

    static bool parse(const std::string& path, std::vector<Sample>& samples);
    static int getSampleCount(const std::string& path);
};
