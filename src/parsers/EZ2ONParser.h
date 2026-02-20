#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "OsuParser.h"

// EZ2ON REBOOT:R chart parser
// File layout (per song folder):
//   *.ezi  - EZFF binary chart (entry point, no encryption)
//   *.ez   - keysound index (plaintext: "id flag filename.wav")
//   *.wav  - keysound/BGM audio files (OGG data with .wav extension)
//
// The .ez keysound index flag field: 0 = keysound, 1 = BGM
// Mode auto-detected from EZFF channel structure: 4K/5K/6K/8K

class EZ2ONParser {
public:
    static bool parse(const std::string& filepath, BeatmapInfo& info);
    static bool isEZ2ONFile(const std::string& filepath);

private:
    // Parse plaintext .ez keysound index (no decryption needed)
    static bool parseKeysoundIndex(const std::string& ezPath,
                                   std::vector<std::string>& sampleMap,
                                   std::string& bgmFilename);

    // Parse EZFF binary chart data
    static bool parseEZFF(const std::vector<uint8_t>& data,
                          const std::string& filepath,
                          int keyCount,
                          const std::vector<int>& playableChannels,
                          BeatmapInfo& info);

    // Auto-detect mode by scanning which channels contain notes
    // Returns {keyCount, vector of playable channel indices}
    static std::pair<int, std::vector<int>> detectMode(const std::vector<uint8_t>& data);

    struct BPMEvent {
        uint32_t tick;
        float bpm;
        double timeMs;
    };
    static double tickToMs(uint32_t tick, const std::vector<BPMEvent>& bpmTimeline,
                           int ticksPerBeat);
};
