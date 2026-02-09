#include "ReplayAnalyzer.h"
#include "../core/OsuMods.h"
#include <cmath>
#include <algorithm>

// Max display time (ms)
const int MAX_SHOWTIME = 160;

std::vector<int> ReplayAnalyzer::getKeyStates(int keyMask, int keyCount) {
    std::vector<int> states(keyCount, 0);
    for (int i = 0; i < keyCount && keyMask > 0; i++) {
        states[i] = keyMask & 1;
        keyMask >>= 1;
    }
    return states;
}

int ReplayAnalyzer::detectKeyCount(const ReplayInfo& replay) {
    // First detect from replay data by finding highest bit used
    int maxKey = 0;
    for (const auto& frame : replay.frames) {
        int keyMask = frame.keyState;
        int key = 0;
        while (keyMask > 0) {
            if (keyMask & 1) {
                maxKey = std::max(maxKey, key + 1);
            }
            keyMask >>= 1;
            key++;
        }
    }

    if (maxKey >= 4 && maxKey <= 10) {
        return maxKey;
    }

    // Fallback: check mods for explicit key count
    if (replay.mods & OsuMods::Key4) return 4;
    if (replay.mods & OsuMods::Key5) return 5;
    if (replay.mods & OsuMods::Key6) return 6;
    if (replay.mods & OsuMods::Key7) return 7;
    if (replay.mods & OsuMods::Key8) return 8;
    if (replay.mods & OsuMods::Key9) return 9;

    return 7;  // Default 7K
}

float ReplayAnalyzer::getSpeedCorrector(int mods) {
    // DT/NC: 1.5x speed -> corrector = 2/3
    if (mods & OsuMods::DoubleTime || mods & OsuMods::Nightcore) {
        return 2.0f / 3.0f;
    }
    // HT: 0.75x speed -> corrector = 4/3
    if (mods & OsuMods::HalfTime) {
        return 4.0f / 3.0f;
    }
    return 1.0f;
}

AnalysisResult ReplayAnalyzer::analyze(const ReplayInfo& replay) {
    AnalysisResult result = {};

    result.keyCount = detectKeyCount(replay);
    result.corrector = getSpeedCorrector(replay.mods);
    result.totalPresses = 0;
    result.maxGameTime = 0;

    analyzePressDistribution(replay, result);
    analyzeRealtimePress(replay, result);

    return result;
}

void ReplayAnalyzer::analyzePressDistribution(const ReplayInfo& replay, AnalysisResult& result) {
    int keyCount = result.keyCount;
    float corrector = result.corrector;

    // Press duration records for each key
    std::vector<std::vector<int>> pressSet(keyCount);

    // Current key states and accumulated time
    std::vector<int> onset(keyCount, 0);
    std::vector<int> timeset(keyCount, 0);

    // Iterate through replay frames
    for (size_t i = 0; i < replay.frames.size(); i++) {
        const auto& frame = replay.frames[i];

        // Skip first few frames
        if (i < 3) continue;

        // Calculate time delta
        int64_t timeDelta = 0;
        if (i > 0) {
            timeDelta = frame.time - replay.frames[i - 1].time;
        }
        if (timeDelta <= 0) continue;

        // Get current key states
        auto currentOnset = getKeyStates(frame.keyState, keyCount);

        // Accumulate key press time
        for (int k = 0; k < keyCount; k++) {
            timeset[k] += onset[k] * (int)timeDelta;

            // Detect key release
            if (onset[k] != 0 && currentOnset[k] == 0) {
                pressSet[k].push_back(timeset[k]);
                timeset[k] = 0;
            }
        }

        onset = currentOnset;
    }

    // Generate distribution data
    int maxTime = (int)std::ceil((MAX_SHOWTIME + 2) / corrector);

    result.pressDistributions.resize(keyCount);
    for (int k = 0; k < keyCount; k++) {
        auto& dist = result.pressDistributions[k];

        // Find max press duration for this key
        int keyMaxTime = maxTime;
        for (int t : pressSet[k]) {
            keyMaxTime = std::max(keyMaxTime, t + 1);
        }

        // Initialize distribution arrays
        dist.basetime.resize(keyMaxTime);
        dist.presscount.resize(keyMaxTime, 0);

        for (int t = 0; t < keyMaxTime; t++) {
            dist.basetime[t] = (int)(t * corrector);
        }

        // Count distribution
        for (int t : pressSet[k]) {
            if (t >= 0 && t < keyMaxTime) {
                dist.presscount[t]++;
                result.totalPresses++;
            }
        }
    }
}

void ReplayAnalyzer::analyzeRealtimePress(const ReplayInfo& replay, AnalysisResult& result) {
    int keyCount = result.keyCount;
    float corrector = result.corrector;

    // Realtime data for each key
    result.realtimePress.resize(keyCount);

    // Current key states and accumulated time
    std::vector<int> onset(keyCount, 0);
    std::vector<int> timeset(keyCount, 0);
    int64_t playTime = 0;

    // Iterate through replay frames
    for (size_t i = 0; i < replay.frames.size(); i++) {
        const auto& frame = replay.frames[i];

        if (i < 3) continue;

        // Calculate time delta
        int64_t timeDelta = 0;
        if (i > 0) {
            timeDelta = frame.time - replay.frames[i - 1].time;
        }
        if (timeDelta <= 0) continue;

        playTime += timeDelta;

        auto currentOnset = getKeyStates(frame.keyState, keyCount);

        for (int k = 0; k < keyCount; k++) {
            timeset[k] += onset[k] * (int)timeDelta;

            // Detect key release
            if (onset[k] != 0 && currentOnset[k] == 0) {
                RealtimePressPoint point;
                point.gameTime = playTime / 1000.0f * corrector;
                point.pressTime = timeset[k] * corrector;
                result.realtimePress[k].push_back(point);

                result.maxGameTime = std::max(result.maxGameTime, point.gameTime);
                timeset[k] = 0;
            }
        }

        onset = currentOnset;
    }
}
