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

    // Extract cue names from ACB file (heuristic, for 1st gen MUSYNX)
    static std::vector<AcbCueInfo> extractCueNames(const std::string& path);

    // Parse ACB UTF tables to build cueName -> MemoryAwbId mapping
    // This is the correct method that handles BGM reordering in 2nd gen ACBs
    static std::unordered_map<std::string, int> parseCueToAwbMapping(const std::string& path);

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

    // Extract and convert using proper UTF-parsed cueName->AwbId mapping
    static bool extractAndConvertMapped(const std::string& acbPath, const std::string& outputDir,
                                        const std::unordered_map<std::string, int>& cueToAwb);

private:
    // Find all HCA blocks in data
    static std::vector<size_t> findHcaBlocks(const uint8_t* data, size_t size);

    // Get HCA block size from header
    static size_t getHcaSize(const uint8_t* data, size_t offset, size_t totalSize);

    // Get HCA duration in milliseconds
    static int64_t getHcaDuration(const uint8_t* data, size_t size);

    // Parse a @UTF table from raw data, returns column names and row data
    struct UtfColumn {
        std::string name;
        uint8_t storageType; // 1=constant, 3=default, 5=per-row
        uint8_t dataType;    // 0-0xB
    };
    struct UtfTable {
        std::string tableName;
        std::vector<UtfColumn> columns;
        uint32_t rowCount;
        // Raw pointers into the data for reading row values
        const uint8_t* data;
        size_t dataSize;
        size_t rowsOffset;    // absolute offset of row data
        size_t stringOffset;  // absolute offset of string table
        size_t dataOffset;    // absolute offset of data area (for blobs)
        size_t schemaEnd;     // where column defs end (start of default values area)
        // Per-row field offsets within a row
        std::vector<size_t> fieldOffsets;
        uint16_t rowLength;
    };
    static bool parseUtfTable(const uint8_t* data, size_t size, size_t baseOffset, UtfTable& table);
    static std::string readUtfString(const UtfTable& table, size_t stringOff);
    static uint32_t readUtfU32(const UtfTable& table, uint32_t row, int colIdx);
    static uint16_t readUtfU16(const UtfTable& table, uint32_t row, int colIdx);
    static std::string readUtfStringField(const UtfTable& table, uint32_t row, int colIdx);
    static std::pair<size_t, size_t> readUtfBlob(const UtfTable& table, uint32_t row, int colIdx);
    static int findColumn(const UtfTable& table, const std::string& name);
};
