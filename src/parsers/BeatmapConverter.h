#pragma once
#include "OsuParser.h"
#include "FastRandom.h"
#include <vector>
#include <set>

// Pattern types for conversion
enum PatternType {
    None = 0,
    ForceStack = 1 << 0,
    ForceNotStack = 1 << 1,
    KeepSingle = 1 << 2,
    LowProbability = 1 << 3,
    Reverse = 1 << 4,
    Cycle = 1 << 5,
    Stair = 1 << 6,
    ReverseStair = 1 << 7,
    Mirror = 1 << 8,
    Gathered = 1 << 9
};

// Converts osu!standard beatmaps to osu!mania
class BeatmapConverter {
public:
    static bool convert(BeatmapInfo& beatmap, int targetKeyCount);
    static int calculateKeyCount(const BeatmapInfo& beatmap);

private:
    static int calculateSeed(const BeatmapInfo& beatmap);
    static int getColumn(float position, int totalColumns, bool allowSpecial = false);
    static double calculateConversionDifficulty(const BeatmapInfo& beatmap);
    static int findAvailableColumn(int initialColumn, int totalColumns,
                                   const std::set<int>& usedColumns, LegacyRandom& random,
                                   int randomStart);
    static int getRandomNoteCount(double p2, double p3, double p4, double p5, LegacyRandom& random);
    static int getRandomNoteCountSlider(double p2, double p3, double p4, LegacyRandom& random);
    static double getBeatLengthAt(double time, const std::vector<TimingPoint>& timingPoints);
};
