#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

// HCA audio data extracted from ACB
struct HcaData {
    std::vector<uint8_t> data;
    std::string cueName;
    int64_t duration;  // in milliseconds
};

// Cue name to HCA index mapping
struct AcbCueInfo {
    std::string name;
    int hcaIndex;
};

// ACB file parser for CRI Atom audio
class AcbParser {
public:
    // Parse ACB file and extract HCA audio data
    static bool parse(const std::string& path, std::vector<HcaData>& hcaFiles);

    // Extract cue names from ACB file
    static std::vector<AcbCueInfo> extractCueNames(const std::string& path);

    // Extract HCA files to a directory (for caching)
    static bool extractToDir(const std::string& acbPath, const std::string& outputDir);

    // Get the BGM (longest audio) from extracted files
    static int findBgmIndex(const std::vector<HcaData>& hcaFiles);

    // Convert HCA file to WAV using FFmpeg
    static bool convertHcaToWav(const std::string& hcaPath, const std::string& wavPath);

    // Extract and convert all HCA to WAV in output directory
    static bool extractAndConvert(const std::string& acbPath, const std::string& outputDir);

    // Extract and convert with cue names from chart file
    static bool extractAndConvert(const std::string& acbPath, const std::string& outputDir,
                                  const std::vector<std::string>& cueNames);

private:
    // Find all HCA blocks in data
    static std::vector<size_t> findHcaBlocks(const uint8_t* data, size_t size);

    // Get HCA block size from header
    static size_t getHcaSize(const uint8_t* data, size_t offset, size_t totalSize);

    // Get HCA duration in milliseconds
    static int64_t getHcaDuration(const uint8_t* data, size_t size);
};
