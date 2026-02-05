#pragma once

// HP system based on osu!stable 2015 September version
// HP range: 0-200, death when targetHP < 100

enum class Judgement;

class HPManager {
public:
    static constexpr double MAX_HP = 200.0;
    static constexpr double MIN_HP = 0.0;
    static constexpr double DEATH_THRESHOLD = 100.0;

    HPManager();

    void reset();
    void setHPDrainRate(double rate);

    // Process judgement HP change
    void processJudgement(Judgement judgement);

    // Hold note specific HP changes
    void processHoldTick(Judgement accuracy);
    void processHoldBreak();

    // Update HP smoothing (call every frame)
    void update(double deltaTime);

    // Getters
    double getCurrentHP() const { return currentHP_; }
    double getTargetHP() const { return targetHP_; }
    double getHPPercent() const { return currentHP_ / MAX_HP; }
    bool isAlive() const { return targetHP_ >= DEATH_THRESHOLD; }
    bool isDead() const { return currentHP_ <= 0.0; }

private:
    double mapDifficultyRange(double difficulty, double min, double mid, double max);

    double currentHP_;      // Current displayed HP (smoothed)
    double targetHP_;       // Target HP (actual value)
    double recoveryRate_;   // HP recovery rate
    double hpDrainRate_;    // HP difficulty parameter (0-10)
};
