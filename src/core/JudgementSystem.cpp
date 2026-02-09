#include "JudgementSystem.h"

JudgementSystem::JudgementSystem()
    : mode_(JudgementMode::BeatmapOD)
{
}

void JudgementSystem::init(JudgementMode mode, float beatmapOD, float customOD,
                           const JudgementConfig* customWindows, double bpm, double clockRate) {
    mode_ = mode;
    clockRate_ = clockRate;  // Save for O2Jam real-time BPM calculation

    switch (mode) {
        case JudgementMode::CustomWindows:
            if (customWindows) {
                windows_.marvelous = customWindows[0].window;
                windows_.perfect = customWindows[1].window;
                windows_.great = customWindows[2].window;
                windows_.good = customWindows[3].window;
                windows_.bad = customWindows[4].window;
                windows_.miss = customWindows[5].window;
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
    if (absDiff <= windows_.marvelous) return Judgement::Marvelous;
    if (absDiff <= windows_.perfect) return Judgement::Perfect;
    if (absDiff <= windows_.great) return Judgement::Great;
    if (absDiff <= windows_.good) return Judgement::Good;
    if (absDiff <= windows_.bad) return Judgement::Bad;
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
