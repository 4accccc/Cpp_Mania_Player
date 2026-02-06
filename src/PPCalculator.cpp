#include "PPCalculator.h"
#include <algorithm>

PPCalculator::PPCalculator()
    : totalObjects(0), starRating(0), currentCombo(0),
      countMAX(0), count300(0), count200(0),
      count100(0), count50(0), countMiss(0) {
}

void PPCalculator::init(int totalNotes, double sr) {
    totalObjects = totalNotes;
    starRating = sr;
    reset();
}

void PPCalculator::reset() {
    currentCombo = 0;
    countMAX = 0;
    count300 = 0;
    count200 = 0;
    count100 = 0;
    count50 = 0;
    countMiss = 0;
}

void PPCalculator::processJudgement(int judgementIndex) {
    // Update judgement counts
    // 0=MAX, 1=300, 2=200, 3=100, 4=50, 5=miss
    switch (judgementIndex) {
        case 0: countMAX++; break;
        case 1: count300++; break;
        case 2: count200++; break;
        case 3: count100++; break;
        case 4: count50++; break;
        case 5: countMiss++; break;
    }

    // Update combo
    if (judgementIndex <= 4) {
        currentCombo++;
    } else {
        currentCombo = 0;
    }
}

double PPCalculator::getAccuracy() const {
    int total = countMAX + count300 + count200 + count100 + count50 + countMiss;
    if (total == 0) return 1.0;

    // acc = (320*MAX + 300*300 + 200*200 + 100*100 + 50*50) / (320 * total)
    double weightedScore = countMAX * 320.0 + count300 * 300.0 + count200 * 200.0 +
                           count100 * 100.0 + count50 * 50.0;
    return weightedScore / (total * 320.0);
}

int PPCalculator::getCurrentPP() const {
    if (starRating <= 0) return 0;

    // 已判定的note数
    int totalHits = countMAX + count300 + count200 + count100 + count50 + countMiss;
    if (totalHits == 0) return 0;

    // 计算当前准确率 (customAcc)
    int numerator = countMAX * 32 + count300 * 30 + count200 * 20 + count100 * 10 + count50 * 5;
    double customAcc = (double)numerator / (totalHits * 32);

    // 星级因子
    double starsFactor = std::pow(std::max(starRating - 0.15, 0.05), 2.2);

    // 准确率因子
    double accFactor = std::max(0.0, 5.0 * customAcc - 4.0);

    // 长度加成 - 使用已判定的note数
    double lengthBonus = 1.0 + 0.1 * std::min(1.0, (double)totalHits / 1500.0);

    // 最终PP
    double pp = 8.0 * starsFactor * accFactor * lengthBonus;

    return static_cast<int>(std::round(pp));
}
