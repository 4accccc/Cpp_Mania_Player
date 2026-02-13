#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "OsuParser.h"

// EZ2AC game modes detected from .ez filename
enum class EZ2ACMode {
    FiveKey,        // 5keymix: Ch3-7 -> 5K
    ScratchMix,     // scratchmix: Ch3-7 -> 5K
    RubyMix,        // rubymix: Ch3-7,Ch10-11 -> 7K
    StreetMix,      // streetmix: Ch3-7,Ch10-11 -> 7K
    FiveRadioMix,   // 5radiomix: Ch3-7,Ch10-11 -> 7K
    SevenStreetMix, // 7streetmix: Ch3-9,Ch10-11 -> 9K
    RadioMix,       // radiomix: Ch3-9,Ch10-11 -> 9K
    ClubMix,        // clubmix: Ch3-7,Ch10-14 -> 10K
    TenRadioMix,    // 10radiomix: Ch3-7,Ch10-14 -> 10K
    SpaceMix,       // spacemix: Ch3-10,Ch12-19 -> 16K
    FourteenRadioMix, // 14radiomix: Ch3-10,Ch12-19 -> 16K
    Catch,          // ez2catch: fruit mode (skip)
    Unknown
};

class EZ2ACParser {
public:
    static bool parse(const std::string& filepath, BeatmapInfo& info);
    static bool isEZ2ACFile(const std::string& filepath);

    // Get mode from .ez filename
    static EZ2ACMode detectMode(const std::string& filename);

    // Get human-readable mode name
    static const char* modeName(EZ2ACMode mode);

    // Get key count for mode
    static int modeKeyCount(EZ2ACMode mode);

private:
    // Decryption
    static std::vector<uint8_t> decrypt(const std::vector<uint8_t>& data);

    // EZFF parsing helpers
    static bool parseEZFF(const std::vector<uint8_t>& data, const std::string& filepath,
                          EZ2ACMode mode, BeatmapInfo& info);

    // Get playable channel list for a mode (maps channel -> lane index)
    // Returns vector of channel indices, position in vector = lane
    static std::vector<int> getPlayableChannels(EZ2ACMode mode);

    // Convert tick position to milliseconds given BPM timeline
    struct BPMEvent {
        uint32_t tick;
        float bpm;
        double timeMs; // computed absolute time
    };
    static double tickToMs(uint32_t tick, const std::vector<BPMEvent>& bpmTimeline,
                           int ticksPerBeat);

    // Parse .ezi keysound index file
    static bool parseEZI(const std::string& eziPath,
                         std::vector<std::string>& sampleMap);

    // Parse .ini difficulty metadata
    static int parseDifficultyLevel(const std::string& iniPath);
};
