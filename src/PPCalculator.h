#pragma once
#include <cmath>

// osu!mania PP Calculator
// Formula: pp = pp_max * (acc - 0.8) * 5
// pp_max = pow(max(sr - 0.15, 0.05), 2.2) * (1 + 0.1 * min(1, notecount / 1500)) * 8
class PPCalculator {
public:
    PPCalculator();

    // Initialize with total note count and star rating
    void init(int totalNotes, double starRating);

    // Reset for new game
    void reset();

    // Process a judgement
    // judgementIndex: 0=MAX, 1=300, 2=200, 3=100, 4=50, 5=miss
    void processJudgement(int judgementIndex);

    // Get current PP value
    int getCurrentPP() const;

    // Get current accuracy (0.0 - 1.0)
    double getAccuracy() const;

private:
    int totalObjects;       // Total notes in beatmap
    double starRating;      // Star rating of the beatmap
    int currentCombo;       // Current combo

    // Judgement counts
    int countMAX;
    int count300;
    int count200;
    int count100;
    int count50;
    int countMiss;
};
