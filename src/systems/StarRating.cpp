#include "StarRating.h"
#include <algorithm>
#include <cmath>

// Factory function
std::unique_ptr<StarRatingCalculator> createStarRatingCalculator(StarRatingVersion version) {
    switch (version) {
        case StarRatingVersion::OsuStable_b20220101:
            return std::make_unique<OsuStable2022StarRating>();
        case StarRatingVersion::OsuStable_b20260101:
        default:
            return std::make_unique<OsuStableStarRating>();
    }
}

// Convenience function
double calculateStarRating(const std::vector<Note>& notes, int keyCount,
                           StarRatingVersion version, double clockRate) {
    auto calculator = createStarRatingCalculator(version);
    return calculator->calculate(notes, keyCount, clockRate);
}

// GClass97.smethod_2: sigmoid function
// maxValue / (1.0 + exp(steepness * (midpoint - x)))
double OsuStableStarRating::sigmoid(double x, double midpoint, double steepness, double maxValue) {
    return maxValue / (1.0 + std::exp(steepness * (midpoint - x)));
}

double OsuStableStarRating::applyDecay(double strain, double deltaTime, double decayBase) {
    if (deltaTime <= 0) return strain;
    return strain * std::pow(decayBase, deltaTime / 1000.0);
}

double OsuStableStarRating::calculate(const std::vector<Note>& notes, int keyCount, double clockRate) {
    if (notes.empty() || keyCount <= 0) {
        return 0.0;
    }

    // Convert notes to difficulty objects
    std::vector<DifficultyObject> objects;
    objects.reserve(notes.size());

    for (const auto& note : notes) {
        if (note.isFakeNote) continue;

        DifficultyObject obj;
        obj.lane = note.lane;
        obj.startTime = static_cast<double>(note.time) / clockRate;  // Apply clockRate
        obj.endTime = note.isHold ? static_cast<double>(note.endTime) / clockRate : obj.startTime;
        obj.isHold = note.isHold;
        obj.deltaTime = 0;  // double_0
        obj.laneDelta = 0;  // double_3
        obj.prevInLane = nullptr;
        objects.push_back(obj);
    }

    // Sort by time
    std::sort(objects.begin(), objects.end(),
        [](const DifficultyObject& a, const DifficultyObject& b) {
            return a.startTime < b.startTime;
        });

    // Calculate strain
    double strain = calculateStrain(objects, keyCount);

    // Apply star rating multiplier
    return strain * STAR_RATING_MULTIPLIER;
}

double OsuStableStarRating::calculateStrain(std::vector<DifficultyObject>& objects, int keyCount) {
    if (objects.empty()) return 0.0;

    // Track previous note in each lane
    std::vector<DifficultyObject*> prevInLane(keyCount, nullptr);

    // Track strain per lane (double_3 array in Class1275)
    std::vector<double> laneStrain(keyCount, 0.0);

    // Overall strains (double_4 = stream, double_5 = jack)
    double streamStrain = 0.0;
    double jackStrain = 1.0;  // IMPORTANT: Initial value is 1.0, not 0.0!

    // Strain peaks for weighted sum
    std::vector<double> strainPeaks;

    double currentSectionEnd = SECTION_LENGTH;
    double currentSectionStrain = 0.0;

    DifficultyObject* prevNote = nullptr;

    for (size_t i = 0; i < objects.size(); i++) {
        DifficultyObject& obj = objects[i];

        // Initialize section end on first note
        if (i == 0) {
            currentSectionEnd = std::ceil(obj.startTime / SECTION_LENGTH) * SECTION_LENGTH;
        }

        // Set previous notes array (class1130_0)
        obj.prevNotes.resize(keyCount, nullptr);
        for (int k = 0; k < keyCount; k++) {
            obj.prevNotes[k] = prevInLane[k];
        }
        obj.prevInLane = prevInLane[obj.lane];

        // Calculate deltaTime (double_0) - time since previous note (any lane)
        if (prevNote) {
            obj.deltaTime = obj.startTime - prevNote->startTime;
        } else {
            obj.deltaTime = obj.startTime;
        }

        // Calculate laneDelta (double_3) - time since previous note in same lane
        if (obj.prevInLane) {
            obj.laneDelta = obj.startTime - obj.prevInLane->startTime;
        } else {
            obj.laneDelta = obj.startTime;
        }

        // Handle section boundaries (cap iterations to prevent infinite loop on corrupted data)
        while (obj.startTime > currentSectionEnd && strainPeaks.size() < 100000) {
            strainPeaks.push_back(currentSectionStrain);
            // Decay strain at section boundary
            if (prevNote) {
                double timeSinceLast = currentSectionEnd - prevNote->startTime;
                currentSectionStrain = applyDecay(streamStrain, timeSinceLast, STREAM_DECAY_BASE) +
                                       applyDecay(jackStrain, timeSinceLast, JACK_DECAY_BASE);
            } else {
                currentSectionStrain = 0.0;
            }
            currentSectionEnd += SECTION_LENGTH;
        }

        // 1. Decay lane strain
        if (obj.lane >= 0 && obj.lane < keyCount) {
            laneStrain[obj.lane] = applyDecay(laneStrain[obj.lane], obj.laneDelta, STREAM_DECAY_BASE);

            // 2. Add stream strain
            double streamValue = calculateStreamStrain(obj);
            laneStrain[obj.lane] += streamValue;

            // 3. Update overall stream strain
            if (obj.deltaTime <= 1.0) {
                streamStrain = std::max(streamStrain, laneStrain[obj.lane]);
            } else {
                streamStrain = laneStrain[obj.lane];
            }
        }

        // 4. Decay jack strain
        jackStrain = applyDecay(jackStrain, obj.deltaTime, JACK_DECAY_BASE);

        // 5. Add jack strain
        double jackValue = calculateJackStrain(obj);
        jackStrain += jackValue;

        // 6. Total strain (stream + jack)
        double totalStrain = streamStrain + jackStrain;
        currentSectionStrain = std::max(currentSectionStrain, totalStrain);

        // Update tracking
        if (obj.lane >= 0 && obj.lane < keyCount) {
            prevInLane[obj.lane] = &obj;
        }
        prevNote = &obj;
    }

    // Add final section
    if (currentSectionStrain > 0) {
        strainPeaks.push_back(currentSectionStrain);
    }

    return calculateWeightedSum(strainPeaks);
}

// Class1072.smethod_0 - Stream strain calculation
// Checks for overlapping hold notes pattern
double OsuStableStarRating::calculateStreamStrain(const DifficultyObject& obj) {
    double multiplier = 1.0;

    // double_ = obj.startTime (double_1)
    // double_2 = obj.endTime (double_2)
    for (auto* prev : obj.prevNotes) {
        if (prev != nullptr) {
            // GClass67.smethod_1(a, b, tol) = (a - tol > b)
            // Check: prev->endTime - 1.0 > obj.endTime AND obj.startTime - 1.0 > prev->startTime
            if (greaterThan(prev->endTime, obj.endTime, 1.0) &&
                greaterThan(obj.startTime, prev->startTime, 1.0)) {
                multiplier = STREAM_CONSECUTIVE_BONUS;  // 1.25
                return STREAM_BASE_VALUE * multiplier;  // 2.0 * 1.25 = 2.5
            }
        }
    }

    return STREAM_BASE_VALUE * multiplier;  // 2.0
}

// Class1010.smethod_0 - Jack strain calculation
double OsuStableStarRating::calculateJackStrain(const DifficultyObject& obj) {
    bool hasJackPattern = false;
    // double_ = obj.startTime, double_2 = obj.endTime
    double minDelta = std::abs(obj.endTime - obj.startTime);
    double multiplier = 1.0;
    double jackBonus = 0.0;

    for (auto* prev : obj.prevNotes) {
        if (prev != nullptr) {
            // Check for jack pattern:
            // prev->endTime - 1.0 > obj.startTime AND
            // obj.endTime - 1.0 > prev->endTime AND
            // obj.startTime - 1.0 > prev->startTime
            bool isJack = greaterThan(prev->endTime, obj.startTime, 1.0) &&
                          greaterThan(obj.endTime, prev->endTime, 1.0) &&
                          greaterThan(obj.startTime, prev->startTime, 1.0);
            hasJackPattern = hasJackPattern || isJack;

            // Check for consecutive bonus (same as stream)
            if (greaterThan(prev->endTime, obj.endTime, 1.0) &&
                greaterThan(obj.startTime, prev->startTime, 1.0)) {
                multiplier = STREAM_CONSECUTIVE_BONUS;  // 1.25
            }

            minDelta = std::min(minDelta, std::abs(obj.endTime - prev->endTime));
        }
    }

    if (hasJackPattern) {
        // sigmoid(minDelta, 30.0, 0.27, 1.0)
        jackBonus = sigmoid(minDelta, 30.0, 0.27, 1.0);
    }

    return (1.0 + jackBonus) * multiplier;
}

// Class1268.vmethod_1 - Weighted sum calculation
double OsuStableStarRating::calculateWeightedSum(const std::vector<double>& strains) {
    if (strains.empty()) return 0.0;

    // Sort strains descending
    std::vector<double> sorted = strains;
    std::sort(sorted.begin(), sorted.end(), std::greater<double>());

    // Weighted sum: sum(strain[i] * 0.9^i)
    double sum = 0.0;
    double weight = 1.0;

    for (double strain : sorted) {
        if (strain > 0) {
            sum += strain * weight;
            weight *= WEIGHT_DECAY;  // 0.9
        }
    }

    return sum;
}

// ============================================================================
// OsuStable2022StarRating - b20220101 implementation
// Based on Class405 and GClass274 from osu! stable
// ============================================================================

double OsuStable2022StarRating::calculate(const std::vector<Note>& notes, int keyCount, double clockRate) {
    if (notes.empty() || keyCount <= 0) {
        return 0.0;
    }

    // Convert notes to difficulty objects
    std::vector<DiffObj> objects;
    objects.reserve(notes.size());

    for (const auto& note : notes) {
        if (note.isFakeNote) continue;

        DiffObj obj;
        obj.lane = note.lane;
        obj.startTime = static_cast<int>(note.time / clockRate);  // Apply clockRate
        obj.endTime = note.isHold ? static_cast<int>(note.endTime / clockRate) : obj.startTime;
        obj.laneEndTimes.resize(keyCount, 0.0);
        obj.laneStrains.resize(keyCount, 0.0);
        obj.overallStrain = 1.0;  // Initial value is 1.0
        objects.push_back(obj);
    }

    // Sort by start time, then by lane (to match osu! behavior for same-time notes)
    std::sort(objects.begin(), objects.end(),
        [](const DiffObj& a, const DiffObj& b) {
            if (a.startTime != b.startTime) return a.startTime < b.startTime;
            return a.lane < b.lane;
        });

    if (objects.size() < 2) {
        return 0.0;
    }

    // Calculate strain for each object (method_2 in Class405)
    // clockRate is now a function parameter

    for (size_t i = 1; i < objects.size(); i++) {
        DiffObj& curr = objects[i];
        const DiffObj& prev = objects[i - 1];

        double deltaTime = static_cast<double>(curr.startTime - prev.startTime);
        double streamDecay = std::pow(STREAM_DECAY_BASE, deltaTime / 1000.0);
        double overallDecay = std::pow(OVERALL_DECAY_BASE, deltaTime / 1000.0);

        double multiplier = 1.0;
        double extraStrain = 0.0;

        // Process each lane
        for (int lane = 0; lane < keyCount; lane++) {
            curr.laneEndTimes[lane] = prev.laneEndTimes[lane];

            // Check for overlap pattern
            if (curr.startTime < curr.laneEndTimes[lane] &&
                curr.endTime > curr.laneEndTimes[lane]) {
                extraStrain = 1.0;
            }
            if (curr.endTime == curr.laneEndTimes[lane]) {
                extraStrain = 0.0;
            }
            if (curr.laneEndTimes[lane] > curr.endTime) {
                multiplier = 1.25;
            }

            // Decay lane strain
            curr.laneStrains[lane] = prev.laneStrains[lane] * streamDecay;
        }

        // Update current lane
        curr.laneEndTimes[curr.lane] = static_cast<double>(curr.endTime);
        curr.laneStrains[curr.lane] += 2.0 * multiplier;

        // Update overall strain
        curr.overallStrain = prev.overallStrain * overallDecay + (1.0 + extraStrain) * multiplier;
    }

    // Calculate weighted sum (vmethod_7 in GClass274)
    double sectionLength = 400.0;
    std::vector<double> strainPeaks;
    double sectionEnd = sectionLength;
    double currentStrain = 0.0;
    const DiffObj* prevObj = nullptr;

    for (size_t i = 1; i < objects.size(); i++) {
        const DiffObj& obj = objects[i];

        while (static_cast<double>(obj.startTime) > sectionEnd && strainPeaks.size() < 100000) {
            if (prevObj == nullptr) {
                currentStrain = 1.0;
            } else {
                strainPeaks.push_back(currentStrain);
                double timeSincePrev = sectionEnd - static_cast<double>(prevObj->startTime);
                double decay0 = std::pow(STREAM_DECAY_BASE, timeSincePrev / 1000.0);
                double decay1 = std::pow(OVERALL_DECAY_BASE, timeSincePrev / 1000.0);
                currentStrain = prevObj->laneStrains[prevObj->lane] * decay0 +
                               prevObj->overallStrain * decay1;
            }
            sectionEnd += sectionLength;
        }

        double objTotalStrain = obj.laneStrains[obj.lane] + obj.overallStrain;
        currentStrain = std::max(objTotalStrain, currentStrain);
        prevObj = &obj;
    }
    strainPeaks.push_back(currentStrain);

    // Sort descending and calculate weighted sum
    std::sort(strainPeaks.begin(), strainPeaks.end(), std::greater<double>());

    double sum = 0.0;
    double weight = 1.0;
    for (double strain : strainPeaks) {
        sum += weight * strain;
        weight *= WEIGHT_DECAY;
    }

    return sum * STAR_RATING_MULTIPLIER;
}
