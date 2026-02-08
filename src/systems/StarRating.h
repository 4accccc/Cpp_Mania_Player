#pragma once
#include <vector>
#include <memory>
#include <cstdint>
#include "Note.h"

// Star Rating calculator version
enum class StarRatingVersion {
    OsuStable_b20260101,  // osu! stable b20260101 - Strain based
    OsuStable_b20220101,  // osu! stable b20220101 - Simpler strain based
};

// Difficulty object for strain calculation
struct DifficultyObject {
    int lane;           // Column index (int_2 in osu!)
    double startTime;   // Note start time / clockRate (double_1)
    double endTime;     // Note end time / clockRate (double_2)
    double deltaTime;   // Time since previous note (any lane) / clockRate (double_0)
    double laneDelta;   // Time since previous note in same lane (double_3)
    bool isHold;

    DifficultyObject* prevInLane;   // Previous note in same lane
    std::vector<DifficultyObject*> prevNotes;  // Previous notes in all lanes (class1130_0)
};

// Base class for star rating calculators
class StarRatingCalculator {
public:
    virtual ~StarRatingCalculator() = default;
    virtual double calculate(const std::vector<Note>& notes, int keyCount, double clockRate = 1.0) = 0;
    virtual std::string getVersionName() const = 0;
};

// osu! stable b20260101 strain-based calculator
class OsuStableStarRating : public StarRatingCalculator {
public:
    double calculate(const std::vector<Note>& notes, int keyCount, double clockRate = 1.0) override;
    std::string getVersionName() const override { return "osu! stable b20260101"; }

private:
    // Strain calculation
    double calculateStrain(std::vector<DifficultyObject>& objects, int keyCount);

    // Stream strain (Class1072)
    static double calculateStreamStrain(const DifficultyObject& obj);

    // Jack strain (Class1010)
    static double calculateJackStrain(const DifficultyObject& obj);

    // Strain decay
    static double applyDecay(double strain, double deltaTime, double decayBase);

    // Weighted sum of strains (Class1268)
    static double calculateWeightedSum(const std::vector<double>& strains);

    // GClass67.smethod_1: a - tolerance > b
    static bool greaterThan(double a, double b, double tolerance = 1.0) {
        return a - tolerance > b;
    }

    // GClass97.smethod_2: sigmoid function
    static double sigmoid(double x, double midpoint, double steepness, double maxValue);

    // Constants
    static constexpr double STAR_RATING_MULTIPLIER = 0.018;
    static constexpr double STREAM_DECAY_BASE = 0.125;
    static constexpr double JACK_DECAY_BASE = 0.3;
    static constexpr double STREAM_BASE_VALUE = 2.0;
    static constexpr double STREAM_CONSECUTIVE_BONUS = 1.25;
    static constexpr double WEIGHT_DECAY = 0.9;
    static constexpr double SECTION_LENGTH = 400.0;
};

// osu! stable b20220101 strain-based calculator (simpler algorithm)
class OsuStable2022StarRating : public StarRatingCalculator {
public:
    double calculate(const std::vector<Note>& notes, int keyCount, double clockRate = 1.0) override;
    std::string getVersionName() const override { return "osu! stable b20220101"; }

private:
    // Difficulty object for b20220101
    struct DiffObj {
        int lane;
        int startTime;
        int endTime;
        std::vector<double> laneEndTimes;  // double_2[] - end time per lane
        std::vector<double> laneStrains;   // double_3[] - strain per lane
        double overallStrain;              // double_4
    };

    // Constants
    static constexpr double STAR_RATING_MULTIPLIER = 0.018;
    static constexpr double STREAM_DECAY_BASE = 0.125;
    static constexpr double OVERALL_DECAY_BASE = 0.3;
    static constexpr double WEIGHT_DECAY = 0.9;
};

// Factory function to create calculator
std::unique_ptr<StarRatingCalculator> createStarRatingCalculator(StarRatingVersion version);

// Convenience function
double calculateStarRating(const std::vector<Note>& notes, int keyCount,
                           StarRatingVersion version = StarRatingVersion::OsuStable_b20260101,
                           double clockRate = 1.0);
