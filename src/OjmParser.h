#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

// OJM sample data
struct OjmSample {
    int id;                     // Sample ID (ref ID for notes)
    std::vector<uint8_t> data;  // Decoded audio data (WAV or OGG)
    bool isOgg;                 // true = OGG, false = WAV
};

// OJM file info
struct OjmInfo {
    std::string format;         // "M30" or "OMC"
    std::unordered_map<int, OjmSample> samples;  // id -> sample
};

// Simple note info for preview generation
struct OjmNoteEvent {
    int64_t timeMs;
    int sampleId;
};

class OjmParser {
public:
    // Parse .ojm file and extract samples
    static bool parse(const std::string& filepath, OjmInfo& info);

    // Check if file is an OJM file
    static bool isOjmFile(const std::string& filepath);

    // Get OJM file path from OJN file path
    static std::string getOjmPath(const std::string& ojnPath);

    // Generate preview audio from OJN+OJM (returns path to generated WAV)
    static std::string generatePreview(const std::string& ojnPath, int durationMs = 30000);

private:
    // Parse M30 format (OGG samples)
    static bool parseM30(const std::vector<uint8_t>& data, OjmInfo& info);

    // Parse OMC format (encrypted WAV samples)
    static bool parseOMC(const std::vector<uint8_t>& data, OjmInfo& info);

    // Decrypt OMC sample data
    static void decryptOMC(std::vector<uint8_t>& data);

    // Decode nami encryption
    static void decodeNami(std::vector<uint8_t>& data);

    // Decode 0412 encryption
    static void decode0412(std::vector<uint8_t>& data);

    // Create WAV header for raw PCM data
    static std::vector<uint8_t> createWavHeader(uint32_t dataSize,
        uint16_t channels, uint32_t sampleRate, uint16_t bitsPerSample);

    // Parse OJN to get note events (simplified, for preview only)
    static std::vector<OjmNoteEvent> parseOjnNotes(const std::string& ojnPath);
};
