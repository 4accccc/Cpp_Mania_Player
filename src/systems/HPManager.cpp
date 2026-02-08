#include "HPManager.h"
#include "Note.h"
#include <algorithm>
#include <cmath>

HPManager::HPManager()
    : currentHP_(MAX_HP)
    , targetHP_(MAX_HP)
    , hpDrainRate_(5.0)
    , transitionStartHP_(MAX_HP)
    , transitionTime_(TRANSITION_DURATION)  // Start fully transitioned
{
}

void HPManager::reset() {
    currentHP_ = MAX_HP;
    targetHP_ = MAX_HP;
    transitionStartHP_ = MAX_HP;
    transitionTime_ = TRANSITION_DURATION;
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
            hpChange = -(hpDrainRate_ + 1.0) * 1.5;
            break;

        case Judgement::Bad:  // 50
            hpChange = -(hpDrainRate_ + 1.0) * 0.32;
            break;

        case Judgement::Good:  // 100
            hpChange = 0.0;
            break;

        case Judgement::Great:  // 200
            hpChange = 1.0 * (0.8 - hpDrainRate_ * 0.08);
            break;

        case Judgement::Perfect:  // 300
            hpChange = 1.0 * (1.0 - hpDrainRate_ * 0.1);
            break;

        case Judgement::Marvelous:  // 300g
            hpChange = 1.0 * (1.1 - hpDrainRate_ * 0.1);
            break;

        default:
            break;
    }

    double newTarget = std::clamp(targetHP_ + hpChange, MIN_HP, MAX_HP);
    if (newTarget != targetHP_) {
        // Start new transition from current displayed HP
        transitionStartHP_ = currentHP_;
        transitionTime_ = 0.0;
        targetHP_ = newTarget;
    }
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

    double newTarget = std::clamp(targetHP_ + hpChange, MIN_HP, MAX_HP);
    if (newTarget != targetHP_) {
        transitionStartHP_ = currentHP_;
        transitionTime_ = 0.0;
        targetHP_ = newTarget;
    }
}

void HPManager::processHoldBreak() {
    double newTarget = std::clamp(targetHP_ - 56.0, MIN_HP, MAX_HP);
    if (newTarget != targetHP_) {
        transitionStartHP_ = currentHP_;
        transitionTime_ = 0.0;
        targetHP_ = newTarget;
    }
}

void HPManager::update(double deltaTime) {
    // lazer-style HP transition: 200ms with OutQuint easing
    double deltaMs = deltaTime * 1000.0;

    if (transitionTime_ < TRANSITION_DURATION) {
        transitionTime_ = std::min(transitionTime_ + deltaMs, TRANSITION_DURATION);

        // Interpolation with OutQuint easing
        double t = transitionTime_ / TRANSITION_DURATION;  // 0 to 1
        double easedT = easeOutQuint(t);

        // Lerp from start to target
        currentHP_ = transitionStartHP_ + (targetHP_ - transitionStartHP_) * easedT;
    } else {
        currentHP_ = targetHP_;
    }
}

double HPManager::easeOutQuint(double t) {
    // OutQuint: 1 - (1 - t)^5
    double inv = 1.0 - t;
    return 1.0 - (inv * inv * inv * inv * inv);
}
