#pragma once
#include "Settings.h"
#include "Note.h"  // For Judgement enum
#include <cstdint>
#include <algorithm>
#include <cmath>

// Judgement windows (in ms)
struct JudgementWindows {
    double marvelous = 16.0;   // 300g
    double perfect = 34.0;     // 300
    double great = 67.0;       // 200
    double good = 97.0;        // 100
    double bad = 121.0;        // 50
    double miss = 158.0;       // miss
};

// Judgement system class
class JudgementSystem {
public:
    JudgementSystem();

    // Initialize judgement windows based on mode and parameters
    void init(JudgementMode mode, float beatmapOD, float customOD,
              const JudgementConfig* customWindows, double bpm, double clockRate);

    // Get judgement from time difference (absolute value)
    Judgement getJudgement(int64_t absDiff) const;

    // Get judgement based on overlap percentage (O2Jam style)
    // overlapPercent: 0.0 to 1.0 (percentage of note overlapping with judgement line)
    Judgement getJudgementByOverlap(double overlapPercent) const;

    // Get current windows
    const JudgementWindows& getWindows() const { return windows_; }

    // Get individual window values
    double getMarvelousWindow() const { return windows_.marvelous; }
    double getPerfectWindow() const { return windows_.perfect; }
    double getGreatWindow() const { return windows_.great; }
    double getGoodWindow() const { return windows_.good; }
    double getBadWindow() const { return windows_.bad; }
    double getMissWindow() const { return windows_.miss; }

    // Get current mode
    JudgementMode getMode() const { return mode_; }

private:
    // Calculate OD-based windows
    void calcODWindows(float od);

    // Apply clock rate scaling
    void applyClockRate(double clockRate);

    JudgementMode mode_;
    JudgementWindows windows_;
    double clockRate_ = 1.0;  // For O2Jam real-time BPM calculation
};
