#pragma once
#include <vector>
#include <cstdint>
#include "../parsers/ReplayParser.h"

// Press time distribution data
struct PressTimeDistribution {
    std::vector<int> basetime;      // Time axis (ms)
    std::vector<int> presscount;    // Press count at each time point
};

// Realtime press data point
struct RealtimePressPoint {
    float gameTime;     // Game time (seconds)
    float pressTime;    // Press duration (ms)
};

// Analysis result
struct AnalysisResult {
    // Press time distribution (one per key)
    std::vector<PressTimeDistribution> pressDistributions;

    // Realtime press time (one set per key)
    std::vector<std::vector<RealtimePressPoint>> realtimePress;

    // Statistics
    int keyCount;           // Detected key count
    float corrector;        // Speed corrector (DT=0.67, HT=1.33, Normal=1.0)
    float maxGameTime;      // Max game time (seconds)
    int totalPresses;       // Total press count
};

class ReplayAnalyzer {
public:
    // Analyze replay data
    static AnalysisResult analyze(const ReplayInfo& replay);

    // Individual analysis functions
    static void analyzePressDistribution(const ReplayInfo& replay, AnalysisResult& result);
    static void analyzeRealtimePress(const ReplayInfo& replay, AnalysisResult& result);

private:
    // Get key states from key mask
    static std::vector<int> getKeyStates(int keyMask, int keyCount);

    // Detect key count used in replay
    static int detectKeyCount(const ReplayInfo& replay);

    // Get speed corrector from mods
    static float getSpeedCorrector(int mods);
};
