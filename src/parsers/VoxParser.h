#pragma once
#include "OsuParser.h"
#include <string>

// Sound Voltex .vox chart parser
// Reads BT (TRACK3-6) and FX (TRACK2,7) notes as 6K mania layout
// Lane mapping: 0=FX-L, 1=BT-A, 2=BT-B, 3=BT-C, 4=BT-D, 5=FX-R
// Analog knobs (TRACK1,8) are ignored
class VoxParser {
public:
    static bool parse(const std::string& path, BeatmapInfo& info);

    // Detect difficulty suffix from filename: 1n=NOV, 2a=ADV, 3e=EXH, 5m=MXM
    static std::string getDifficultyName(const std::string& filename);

    // Get song ID from folder name (e.g. "2348_l_ice" -> 2348)
    static int getSongIdFromPath(const std::string& path);

private:
    struct BpmChange {
        int tick;       // absolute tick position
        double bpm;
    };

    struct TimeSignature {
        int tick;       // absolute tick position
        int numerator;
        int denominator;
    };

    // Convert measure,beat,offset to absolute tick
    // Needs time signature info for variable measure lengths
    static int positionToTick(int measure, int beat, int offset,
                              const std::vector<TimeSignature>& timeSigs);

    // Convert absolute tick to milliseconds using BPM changes
    static int64_t tickToMs(int tick, const std::vector<BpmChange>& bpmChanges);

    // Track index to lane: TRACK2->0, TRACK3->1, ..., TRACK7->5
    static int trackToLane(int trackIndex);
};
