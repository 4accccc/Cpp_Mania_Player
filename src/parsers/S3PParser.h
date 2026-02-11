#pragma once
#include <string>
#include <vector>
#include <cstdint>

// S3P file format parser for IIDX keysounds
// S3P contains multiple WMA audio samples
class S3PParser {
public:
    struct Sample {
        uint32_t offset;      // Offset in file
        int32_t size;         // Size of entry
        uint32_t waveOffset;  // Offset of actual wave data
        int32_t waveSize;     // Size of wave data
    };

    // Parse S3P file and return sample info
    static bool parse(const std::string& path, std::vector<Sample>& samples);

    // Extract a single sample's wave data
    static bool extractSample(const std::string& path, int index, std::vector<uint8_t>& waveData);

    // Get number of samples in file
    static int getSampleCount(const std::string& path);
};
