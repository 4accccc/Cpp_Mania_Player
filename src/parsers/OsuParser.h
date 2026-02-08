#pragma once
#include <string>
#include <vector>
#include "Note.h"

struct TimingPoint {
    double time;
    double beatLength;
    bool uninherited;
    double effectiveBeatLength;  // For SV calculation: baseBeatLength / svMultiplier
    SampleSet sampleSet = SampleSet::Normal;  // Default sample set for hitsounds
    int volume = 100;  // Volume percentage
};

// Storyboard sound sample (auto-played at specific time)
struct StoryboardSample {
    int64_t time;
    std::string filename;
    int volume;
    int sampleHandle = -1;  // Filled by KeySoundManager
    int fallbackHandle = -1;  // Fallback sample ID for O2Jam
};

struct BeatmapInfo {
    std::string audioFilename;
    std::string title;
    std::string artist;
    std::string creator;
    std::string version;  // Difficulty name
    std::string beatmapHash;  // MD5 hash of .osu file
    int mode;
    int keyCount;
    float od;
    float cs;  // Circle Size (for conversion seed)
    float ar;  // Approach Rate (for conversion seed)
    float hp;  // HP Drain (for conversion seed)
    float sliderMultiplier;   // For slider duration calculation
    int totalObjectCount;      // Total hit objects
    int endTimeObjectCount;    // Sliders + Spinners (objects with duration)
    std::vector<Note> notes;
    std::vector<TimingPoint> timingPoints;  // For density calculation
    std::vector<StoryboardSample> storyboardSamples;  // Auto-played sounds
};

class OsuParser {
public:
    static bool parse(const std::string& filepath, BeatmapInfo& info);
    static bool isMania(const BeatmapInfo& info);
    static std::string calculateMD5(const std::string& filepath);

private:
    static int xToLane(int x, int keyCount);
    static std::string trim(const std::string& str);
};
