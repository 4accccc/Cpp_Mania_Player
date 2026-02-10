#include "JudgementSystem.h"

JudgementSystem::JudgementSystem()
    : mode_(JudgementMode::BeatmapOD)
{
}

void JudgementSystem::init(JudgementMode mode, float beatmapOD, float customOD,
                           const JudgementConfig* customWindows, double bpm, double clockRate) {
    mode_ = mode;
    clockRate_ = clockRate;  // Save for O2Jam real-time BPM calculation

    // Reset enabled state - default all enabled
    for (int i = 0; i < 6; i++) {
        enabled_[i] = true;
    }

    switch (mode) {
        case JudgementMode::CustomWindows:
            if (customWindows) {
                windows_.marvelous = customWindows[0].window;
                windows_.perfect = customWindows[1].window;
                windows_.great = customWindows[2].window;
                windows_.good = customWindows[3].window;
                windows_.bad = customWindows[4].window;
                windows_.miss = customWindows[5].window;
                // Save enabled state for each judgement level (only in CustomWindows mode)
                for (int i = 0; i < 6; i++) {
                    enabled_[i] = customWindows[i].enabled;
                }
            }
            break;

        case JudgementMode::O2Jam:
            // O2Jam uses overlap-based judgement, no time windows needed
            break;

        case JudgementMode::CustomOD:
            calcODWindows(customOD);
            break;

        case JudgementMode::BeatmapOD:
        default:
            calcODWindows(beatmapOD);
            break;
    }

    applyClockRate(clockRate);
}

void JudgementSystem::calcODWindows(float od) {
    windows_.marvelous = 16.0;
    windows_.perfect = 64.0 - 3.0 * od;
    windows_.great = 97.0 - 3.0 * od;
    windows_.good = 127.0 - 3.0 * od;
    windows_.bad = 151.0 - 3.0 * od;
    windows_.miss = 188.0 - 3.0 * od;
}

void JudgementSystem::applyClockRate(double clockRate) {
    windows_.marvelous *= clockRate;
    windows_.perfect *= clockRate;
    windows_.great *= clockRate;
    windows_.good *= clockRate;
    windows_.bad *= clockRate;
    windows_.miss *= clockRate;
}

Judgement JudgementSystem::getJudgement(int64_t absDiff) const {
    // Check each judgement level, skip if disabled
    if (absDiff <= windows_.marvelous && enabled_[0]) return Judgement::Marvelous;
    if (absDiff <= windows_.perfect && enabled_[1]) return Judgement::Perfect;
    if (absDiff <= windows_.great && enabled_[2]) return Judgement::Great;
    if (absDiff <= windows_.good && enabled_[3]) return Judgement::Good;
    if (absDiff <= windows_.bad && enabled_[4]) return Judgement::Bad;
    return Judgement::Miss;
}

Judgement JudgementSystem::getJudgementByOverlap(double overlapPercent) const {
    // O2Jam overlap-based judgement:
    // Cool: >=80%, Good: >=50%, Bad: >=20%, Miss: <20%
    if (overlapPercent >= 0.80) return Judgement::Marvelous;  // Cool -> 300g
    if (overlapPercent >= 0.50) return Judgement::Great;      // Good -> 200
    if (overlapPercent >= 0.20) return Judgement::Bad;        // Bad -> 50
    return Judgement::Miss;
}

Judgement JudgementSystem::adjustForEnabled(Judgement j) const {
    // Downgrade judgement if the level is disabled
    // Judgement enum: None=0, Marvelous=1, Perfect=2, Great=3, Good=4, Bad=5, Miss=6
    // enabled_ array: [0]=300g, [1]=300, [2]=200, [3]=100, [4]=50, [5]=miss
    // So enabled index = Judgement value - 1
    if (j == Judgement::None || j == Judgement::Miss) return j;

    int idx = static_cast<int>(j) - 1;  // Convert to enabled_ index
    while (idx < 5 && !enabled_[idx]) {
        idx++;
    }
    return static_cast<Judgement>(idx + 1);  // Convert back to Judgement
}

double JudgementSystem::getMaxEnabledWindow() const {
    // Return the largest enabled judgement window (excluding miss)
    // Check from bad to marvelous (largest to smallest)
    if (enabled_[4]) return windows_.bad;       // 50
    if (enabled_[3]) return windows_.good;      // 100
    if (enabled_[2]) return windows_.great;     // 200
    if (enabled_[1]) return windows_.perfect;   // 300
    if (enabled_[0]) return windows_.marvelous; // 300g
    return windows_.bad;  // Fallback to bad window
}
