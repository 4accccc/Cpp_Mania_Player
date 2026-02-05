#include "HPManager.h"
#include "Note.h"
#include <algorithm>
#include <cmath>

HPManager::HPManager()
    : currentHP_(MAX_HP)
    , targetHP_(MAX_HP)
    , recoveryRate_(0.02)  // osu! default recovery rate
    , hpDrainRate_(5.0)
{
}

void HPManager::reset() {
    currentHP_ = MAX_HP;
    targetHP_ = MAX_HP;
}

void HPManager::setHPDrainRate(double rate) {
    hpDrainRate_ = std::clamp(rate, 0.0, 10.0);
}

double HPManager::mapDifficultyRange(double difficulty, double min, double mid, double max) {
    if (difficulty > 5.0)
        return mid + (max - mid) * (difficulty - 5.0) / 5.0;
    if (difficulty < 5.0)
        return mid - (mid - min) * (5.0 - difficulty) / 5.0;
    return mid;
}

void HPManager::processJudgement(Judgement judgement) {
    double hpChange = 0.0;

    switch (judgement) {
        case Judgement::Miss:
            // Miss: -(HP + 1) × 1.5
            hpChange = -(hpDrainRate_ + 1.0) * 1.5;
            break;

        case Judgement::Bad:  // 50
            // 50: -(HP + 1) × 0.32
            hpChange = -(hpDrainRate_ + 1.0) * 0.32;
            break;

        case Judgement::Good:  // 100
            // 100: 不变
            hpChange = 0.0;
            break;

        case Judgement::Great:  // 200
            // 200: base × (0.8 - HP × 0.08)
            hpChange = 1.0 * (0.8 - hpDrainRate_ * 0.08);
            break;

        case Judgement::Perfect:  // 300
            // 300: base × (1.0 - HP × 0.1)
            hpChange = 1.0 * (1.0 - hpDrainRate_ * 0.1);
            break;

        case Judgement::Marvelous:  // 300g
            // 300g: base × (1.1 - HP × 0.1)
            hpChange = 1.0 * (1.1 - hpDrainRate_ * 0.1);
            break;

        default:
            break;
    }

    targetHP_ = std::clamp(targetHP_ + hpChange, MIN_HP, MAX_HP);
}

void HPManager::processHoldTick(Judgement accuracy) {
    double hpChange = 0.0;

    switch (accuracy) {
        case Judgement::Marvelous:  // 300g
            hpChange = 2.0;
            break;
        case Judgement::Perfect:  // 300
            hpChange = 1.0;
            break;
        case Judgement::Great:  // 200
            hpChange = -8.0;
            break;
        default:
            break;
    }

    targetHP_ = std::clamp(targetHP_ + hpChange, MIN_HP, MAX_HP);
}

void HPManager::processHoldBreak() {
    targetHP_ = std::clamp(targetHP_ - 56.0, MIN_HP, MAX_HP);
}

void HPManager::update(double deltaTime) {
    // osu! style HP transition
    // deltaTime is in seconds, convert to osu! scale factor
    // osu!: double_0 = deltaTime_ms / 16.6667 (relative to 60fps)
    double deltaMs = deltaTime * 1000.0;
    double scaleFactor = deltaMs / 16.6667;

    if (currentHP_ < targetHP_) {
        // Recovery: linear increase
        // osu!: displayHealth += 0.02 * deltaTime_ms
        currentHP_ = std::min(MAX_HP, currentHP_ + recoveryRate_ * deltaMs);
        if (currentHP_ > targetHP_) {
            currentHP_ = targetHP_;
        }
    }
    else if (currentHP_ > targetHP_) {
        // Drain: exponential decay
        // osu!: displayHealth -= diff / 6.0 * double_0
        double diff = currentHP_ - targetHP_;
        currentHP_ = std::max(MIN_HP, currentHP_ - (diff / 6.0) * scaleFactor);
        if (currentHP_ < targetHP_) {
            currentHP_ = targetHP_;
        }
    }
}
