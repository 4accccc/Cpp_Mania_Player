#include "BeatmapConverter.h"
#include <cmath>
#include <algorithm>
#include <iostream>

int BeatmapConverter::calculateSeed(const BeatmapInfo& beatmap) {
    int seed = static_cast<int>(std::round(beatmap.hp + beatmap.cs)) * 20
             + static_cast<int>(beatmap.od * 41.2)
             + static_cast<int>(std::round(beatmap.ar));
    return seed;
}

int BeatmapConverter::calculateKeyCount(const BeatmapInfo& beatmap) {
    if (beatmap.mode == 3) {
        return std::max(1, static_cast<int>(std::round(beatmap.cs)));
    }

    double roundedOD = std::round(beatmap.od);
    double roundedCS = std::round(beatmap.cs);

    if (beatmap.totalObjectCount > 0 && beatmap.endTimeObjectCount >= 0) {
        double percentSpecial = static_cast<double>(beatmap.endTimeObjectCount)
                              / beatmap.totalObjectCount;

        if (percentSpecial < 0.2)
            return 7;
        if (percentSpecial < 0.3 || roundedCS >= 5)
            return roundedOD > 5 ? 7 : 6;
        if (percentSpecial > 0.6)
            return roundedOD > 4 ? 5 : 4;
    }

    return std::max(4, std::min(static_cast<int>(roundedOD) + 1, 7));
}

int BeatmapConverter::getColumn(float position, int totalColumns, bool allowSpecial) {
    // Special handling for 8K with allowSpecial
    if (allowSpecial && totalColumns == 8) {
        float localXDivisor = 512.0f / 7;
        int column = static_cast<int>(std::floor(position / localXDivisor));
        return std::clamp(column, 0, 6) + 1;
    }

    float localXDivisor = 512.0f / totalColumns;
    int column = static_cast<int>(std::floor(position / localXDivisor));
    return std::clamp(column, 0, totalColumns - 1);
}

double BeatmapConverter::calculateConversionDifficulty(const BeatmapInfo& beatmap) {
    if (beatmap.notes.empty()) return 1.0;

    int64_t firstTime = beatmap.notes.front().time;
    int64_t lastTime = beatmap.notes.back().time;
    int drainTime = static_cast<int>((lastTime - firstTime) / 1000);
    if (drainTime == 0) drainTime = 10000;

    double ar = std::clamp(static_cast<double>(beatmap.ar), 4.0, 7.0);
    double difficulty = ((beatmap.hp + ar) / 1.5 +
                        static_cast<double>(beatmap.notes.size()) / drainTime * 9.0) / 38.0 * 5.0 / 1.15;
    return std::min(difficulty, 12.0);
}

int BeatmapConverter::findAvailableColumn(int initialColumn, int totalColumns,
                                          const std::set<int>& usedColumns, LegacyRandom& random,
                                          int randomStart) {
    // If initial column is available, return it (no random call)
    if (usedColumns.find(initialColumn) == usedColumns.end()) {
        return initialColumn;
    }

    // Check if any column is available
    bool hasAvailable = false;
    for (int i = randomStart; i < totalColumns; i++) {
        if (usedColumns.find(i) == usedColumns.end()) {
            hasAvailable = true;
            break;
        }
    }
    if (!hasAvailable) return initialColumn;

    // Random search until we find available column
    int col;
    do {
        col = random.next(randomStart, totalColumns);
    } while (usedColumns.find(col) != usedColumns.end());

    return col;
}

int BeatmapConverter::getRandomNoteCount(double p2, double p3, double p4, double p5, LegacyRandom& random) {
    double val = random.nextDouble();
    // Check from highest to lowest (p5, p4, p3, p2)
    if (val >= 1 - p5) return 5;
    if (val >= 1 - p4) return 4;
    if (val >= 1 - p3) return 3;
    if (val >= 1 - p2) return 2;
    return 1;
}

// For Slider: only 3 probability parameters (p2, p3, p4)
int BeatmapConverter::getRandomNoteCountSlider(double p2, double p3, double p4, LegacyRandom& random) {
    double val = random.nextDouble();
    if (val >= 1 - p4) return 4;
    if (val >= 1 - p3) return 3;
    if (val >= 1 - p2) return 2;
    return 1;
}

double BeatmapConverter::getBeatLengthAt(double time, const std::vector<TimingPoint>& timingPoints) {
    double beatLength = 500.0; // default 120 BPM
    for (const auto& tp : timingPoints) {
        if (tp.time <= time && tp.uninherited) {
            beatLength = tp.beatLength;
        }
    }
    return beatLength;
}

bool BeatmapConverter::convert(BeatmapInfo& beatmap, int targetKeyCount) {
    if (beatmap.mode == 3) {
        return true;
    }

    int seed = calculateSeed(beatmap);
    LegacyRandom random(seed);
    double convDiff = calculateConversionDifficulty(beatmap);

    // RandomStart is 1 for 8K, 0 otherwise
    int randomStart = (targetKeyCount == 8) ? 1 : 0;

    std::vector<Note> convertedNotes;
    convertedNotes.reserve(beatmap.notes.size() * 2);

    int lastColumn = 0;
    std::set<int> previousColumns;
    int64_t lastTime = 0;
    float lastX = 256.0f;
    int lastStair = Stair;

    // Density calculation using sliding window queue (max 7 elements)
    const int MAX_NOTES_FOR_DENSITY = 7;
    std::vector<double> prevNoteTimes;
    prevNoteTimes.reserve(MAX_NOTES_FOR_DENSITY);
    double density = std::numeric_limits<double>::max();

    // Lambda to compute density
    auto computeDensity = [&](double newNoteTime) {
        if (prevNoteTimes.size() >= MAX_NOTES_FOR_DENSITY) {
            prevNoteTimes.erase(prevNoteTimes.begin());
        }
        prevNoteTimes.push_back(newNoteTime);
        if (prevNoteTimes.size() >= 2) {
            density = (prevNoteTimes.back() - prevNoteTimes.front()) / prevNoteTimes.size();
        }
    };

    // Lambda to record note
    auto recordNote = [&](double time, float posX) {
        lastTime = static_cast<int64_t>(time);
        lastX = posX;
    };

    for (size_t i = 0; i < beatmap.notes.size(); i++) {
        const Note& note = beatmap.notes[i];
        float posX = note.x;
        int64_t timeSep = (i > 0) ? (note.time - lastTime) : 1000;
        float posSep = std::abs(posX - lastX);

        // Debug output for first 60 objects
        if (i < 60) {
            std::cerr << "Object " << i << ": type=" << (int)note.objectType
                      << " time=" << note.time << " x=" << posX
                      << " timeSep=" << timeSep << " posSep=" << posSep
                      << " lastTime=" << lastTime << std::endl;
        }

        // Get beat length for HitCircle convertType calculation
        double beatLength = getBeatLengthAt(note.time, beatmap.timingPoints);

        std::set<int> currentColumns;
        int column;
        int64_t noteEndTime = note.endTime;

        // ========== SLIDER CONVERSION ==========
        if (note.objectType == ObjectType::Slider) {
            int convertType = None;
            // Slider only has LowProbability if not in Kiai mode (we don't track Kiai)
            convertType = LowProbability;

            int segDur = note.segmentDuration;
            int spanCount = note.spanCount;

            // Call recordNote and computeDensity for each span BEFORE generating pattern
            for (int s = 0; s <= spanCount; s++) {
                double time = note.time + segDur * s;
                recordNote(time, posX);
                computeDensity(time);
            }

            if (spanCount > 1) {
                // Multi-span slider logic
                if (segDur <= 90) {
                    // generateRandomHoldNotes(1)
                    int usable = targetKeyCount - randomStart - (int)previousColumns.size();
                    column = random.next(randomStart, targetKeyCount);
                    if (usable > 0) {
                        std::set<int> blocked = currentColumns;
                        for (int c : previousColumns) blocked.insert(c);
                        column = findAvailableColumn(column, targetKeyCount, blocked, random, randomStart);
                    } else {
                        column = findAvailableColumn(column, targetKeyCount, currentColumns, random, randomStart);
                    }
                    currentColumns.insert(column);
                } else if (segDur <= 120) {
                    // generateRandomNotes with ForceNotStack
                    int noteCount = spanCount + 1;
                    column = getColumn(posX, targetKeyCount, true);
                    if ((int)previousColumns.size() < targetKeyCount) {
                        std::set<int> blocked;
                        for (int c : previousColumns) blocked.insert(c);
                        column = findAvailableColumn(column, targetKeyCount, blocked, random, randomStart);
                    }
                    int lastCol = column;
                    int lastNoteCol = column;  // Track the last note's column
                    int startT = (int)note.time;
                    for (int n = 0; n < noteCount; n++) {
                        lastNoteCol = column;  // Save before generating note
                        Note newNote(column, startT, false, startT);
                        convertedNotes.push_back(newNote);
                        // Find next column != lastCol
                        std::set<int> blocked;
                        blocked.insert(lastCol);
                        column = findAvailableColumn(column, targetKeyCount, blocked, random, randomStart);
                        lastCol = column;
                        startT += segDur;
                    }
                    currentColumns.insert(lastNoteCol);
                    goto slider_done;
                } else if (segDur <= 160) {
                    // generateStair
                    column = getColumn(posX, targetKeyCount, true);
                    bool increasing = random.nextDouble() > 0.5;
                    int startT = (int)note.time;
                    int lastNoteCol = column;
                    for (int n = 0; n <= spanCount; n++) {
                        lastNoteCol = column;  // Save before updating
                        Note newNote(column, startT, false, startT);
                        convertedNotes.push_back(newNote);
                        startT += segDur;
                        if (increasing) {
                            if (column >= targetKeyCount - 1) { increasing = false; column--; }
                            else column++;
                        } else {
                            if (column <= randomStart) { increasing = true; column++; }
                            else column--;
                        }
                    }
                    currentColumns.insert(lastNoteCol);
                    goto slider_done;
                } else if (segDur <= 200 && convDiff > 3) {
                    // generateRandomMultipleNotes
                    bool legacy = targetKeyCount >= 4 && targetKeyCount <= 8;
                    int interval = random.next(1, targetKeyCount - (legacy ? 1 : 0));
                    column = getColumn(posX, targetKeyCount, true);
                    int startT = (int)note.time;
                    int lastSpanCol1 = column;  // First note's column in last span
                    int lastSpanCol2 = column;  // Second note's column in last span
                    for (int n = 0; n <= spanCount; n++) {
                        lastSpanCol1 = column;  // Save first note's column
                        Note newNote(column, startT, false, startT);
                        convertedNotes.push_back(newNote);
                        column += interval;
                        if (column >= targetKeyCount - randomStart)
                            column = column - targetKeyCount - randomStart + (legacy ? 1 : 0);
                        column += randomStart;
                        if (targetKeyCount > 2) {
                            lastSpanCol2 = column;  // Save second note's column
                            Note newNote2(column, startT, false, startT);
                            convertedNotes.push_back(newNote2);
                        }
                        column = random.next(randomStart, targetKeyCount);
                        startT += segDur;
                    }
                    // endTimePattern contains notes from last span (endTime == EndTime)
                    currentColumns.insert(lastSpanCol1);
                    if (targetKeyCount > 2) {
                        currentColumns.insert(lastSpanCol2);
                    }
                    goto slider_done;
                } else {
                    double duration = note.endTime - note.time;
                    if (duration >= 4000) {
                        // generateNRandomNotes(0.23, 0, 0)
                        double p2 = 0.23, p3 = 0, p4 = 0;
                        // Adjust by TotalColumns
                        if (targetKeyCount == 2) { p2 = 0; }
                        else if (targetKeyCount == 3) { p2 = std::min(p2, 0.1); }
                        else if (targetKeyCount == 4) { p2 = std::min(p2, 0.3); }
                        else if (targetKeyCount == 5) { p2 = std::min(p2, 0.34); }
                        int noteCount = getRandomNoteCountSlider(p2, p3, p4, random);
                        int usable = targetKeyCount - randomStart - (int)previousColumns.size();
                        column = random.next(randomStart, targetKeyCount);
                        for (int n = 0; n < std::min(usable, noteCount); n++) {
                            std::set<int> blocked = currentColumns;
                            for (int c : previousColumns) blocked.insert(c);
                            column = findAvailableColumn(column, targetKeyCount, blocked, random, randomStart);
                            currentColumns.insert(column);
                        }
                        for (int n = 0; n < noteCount - usable; n++) {
                            column = findAvailableColumn(column, targetKeyCount, currentColumns, random, randomStart);
                            currentColumns.insert(column);
                        }
                    } else if (segDur > 400 && spanCount < targetKeyCount - 1 - randomStart) {
                        // generateTiledHoldNotes
                        // Note: No ForceNotStack check here - convertType doesn't have it
                        int colRepeat = std::min(spanCount, targetKeyCount);
                        int endT = (int)note.time + segDur * spanCount;
                        column = getColumn(posX, targetKeyCount, true);
                        int startT = (int)note.time;
                        for (int n = 0; n < colRepeat; n++) {
                            column = findAvailableColumn(column, targetKeyCount, currentColumns, random, randomStart);
                            Note newNote(column, startT, true, endT);
                            convertedNotes.push_back(newNote);
                            currentColumns.insert(column);
                            startT += segDur;
                        }
                        goto slider_done;
                    } else {
                        // generateHoldAndNormalNotes
                        // Note: No ForceNotStack check here - convertType doesn't have it
                        int holdColumn = getColumn(posX, targetKeyCount, true);

                        // Create hold note
                        Note holdNote(holdColumn, note.time, true, note.endTime);
                        convertedNotes.push_back(holdNote);

                        // GetRandomColumn() call
                        int nextColumn = random.next(randomStart, targetKeyCount);

                        // GetRandomNoteCount() call
                        int noteCount = 0;
                        if (convDiff > 6.5) {
                            noteCount = getRandomNoteCount(0.63, 0, 0, 0, random);
                        } else if (convDiff > 4) {
                            noteCount = getRandomNoteCount(targetKeyCount < 6 ? 0.12 : 0.45, 0, 0, 0, random);
                        } else if (convDiff > 2.5) {
                            noteCount = getRandomNoteCount(targetKeyCount < 6 ? 0 : 0.24, 0, 0, 0, random);
                        }
                        noteCount = std::min(targetKeyCount - 1, noteCount);

                        // Generate normal notes for each span
                        int startT = (int)note.time;
                        for (int s = 0; s <= spanCount; s++) {
                            for (int j = 0; j < noteCount; j++) {
                                // FindAvailableColumn with validation c != holdColumn
                                std::set<int> blocked;
                                blocked.insert(holdColumn);
                                nextColumn = findAvailableColumn(nextColumn, targetKeyCount, blocked, random, randomStart);
                                Note newNote(nextColumn, startT, false, startT);
                                convertedNotes.push_back(newNote);
                            }
                            startT += segDur;
                        }

                        currentColumns.insert(holdColumn);
                    }
                }
            } else {
                // Single span slider (SpanCount == 1)
                if (segDur <= 110) {
                    // generateRandomNotes
                    int noteCount = (segDur < 80) ? 1 : 2;
                    column = getColumn(posX, targetKeyCount, true);
                    bool forceNotStack = (int)previousColumns.size() < targetKeyCount;
                    if (forceNotStack) {
                        std::set<int> blocked;
                        for (int c : previousColumns) blocked.insert(c);
                        column = findAvailableColumn(column, targetKeyCount, blocked, random, randomStart);
                    }
                    int lastCol = column;
                    int lastNoteCol = column;
                    int startT = (int)note.time;
                    for (int n = 0; n < noteCount; n++) {
                        lastNoteCol = column;
                        Note newNote(column, startT, false, startT);
                        convertedNotes.push_back(newNote);
                        std::set<int> blocked;
                        blocked.insert(lastCol);
                        column = findAvailableColumn(column, targetKeyCount, blocked, random, randomStart);
                        lastCol = column;
                        startT += segDur;
                    }
                    currentColumns.insert(lastNoteCol);
                    goto slider_done;
                } else {
                    // generateNRandomNotes based on ConversionDifficulty
                    double p2, p3, p4;
                    bool lowProb = (convertType & LowProbability) != 0;
                    if (convDiff > 6.5) {
                        if (lowProb) { p2 = 0.78; p3 = 0.3; p4 = 0; }
                        else { p2 = 0.85; p3 = 0.36; p4 = 0.03; }
                    } else if (convDiff > 4) {
                        if (lowProb) { p2 = 0.43; p3 = 0.08; p4 = 0; }
                        else { p2 = 0.56; p3 = 0.18; p4 = 0; }
                    } else if (convDiff > 2.5) {
                        if (lowProb) { p2 = 0.3; p3 = 0; p4 = 0; }
                        else { p2 = 0.37; p3 = 0.08; p4 = 0; }
                    } else {
                        if (lowProb) { p2 = 0.17; p3 = 0; p4 = 0; }
                        else { p2 = 0.27; p3 = 0; p4 = 0; }
                    }
                    // Adjust by TotalColumns
                    if (targetKeyCount == 2) { p2 = 0; p3 = 0; p4 = 0; }
                    else if (targetKeyCount == 3) { p2 = std::min(p2, 0.1); p3 = 0; p4 = 0; }
                    else if (targetKeyCount == 4) { p2 = std::min(p2, 0.3); p3 = std::min(p3, 0.04); p4 = 0; }
                    else if (targetKeyCount == 5) { p2 = std::min(p2, 0.34); p3 = std::min(p3, 0.1); p4 = std::min(p4, 0.03); }

                    // If has clap/finish and not LowProbability, p2 = 1 (from SliderPatternGenerator.generateNRandomNotes)
                    bool canGenerateTwoNotes = !lowProb && (note.hasClap || note.hasFinish);
                    if (canGenerateTwoNotes) {
                        p2 = 1;
                    }

                    int noteCount = getRandomNoteCountSlider(p2, p3, p4, random);
                    int usable = targetKeyCount - randomStart - (int)previousColumns.size();
                    // GetRandomColumn() for initial column
                    column = random.next(randomStart, targetKeyCount);
                    for (int n = 0; n < std::min(usable, noteCount); n++) {
                        std::set<int> blocked = currentColumns;
                        for (int c : previousColumns) blocked.insert(c);
                        column = findAvailableColumn(column, targetKeyCount, blocked, random, randomStart);
                        currentColumns.insert(column);
                    }
                    for (int n = 0; n < noteCount - usable; n++) {
                        column = findAvailableColumn(column, targetKeyCount, currentColumns, random, randomStart);
                        currentColumns.insert(column);
                    }
                }
            }
            // Create hold notes for slider
            for (int col : currentColumns) {
                Note newNote(col, note.time, true, note.endTime);
                convertedNotes.push_back(newNote);
            }
            slider_done:
            if (!currentColumns.empty()) {
                lastColumn = *currentColumns.rbegin();
                previousColumns = currentColumns;
            }
            // lastTime and lastX already updated by recordNote calls above
            continue;
        }

        // ========== SPINNER CONVERSION ==========
        if (note.objectType == ObjectType::Spinner) {
            int convertType = None;
            if ((int)previousColumns.size() < targetKeyCount) {
                convertType = ForceNotStack;
            }

            bool generateHold = (note.endTime - note.time) >= 100;
            int lowerBound = (targetKeyCount == 8) ? 1 : 0;

            // GetRandomColumn for initial column
            column = random.next(lowerBound, targetKeyCount);
            if (convertType & ForceNotStack) {
                std::set<int> blocked;
                for (int c : previousColumns) blocked.insert(c);
                column = findAvailableColumn(column, targetKeyCount, blocked, random, lowerBound);
            } else {
                std::set<int> empty;
                column = findAvailableColumn(column, targetKeyCount, empty, random, lowerBound);
            }

            currentColumns.insert(column);
            Note newNote(column, note.time, generateHold, generateHold ? note.endTime : note.time);
            convertedNotes.push_back(newNote);

            // Spinner uses fixed position (256, 192) and does NOT update previousColumns
            recordNote(note.endTime, 256.0f);
            computeDensity(note.endTime);
            // Note: Spinner does NOT update previousColumns (lastPattern)
            continue;
        }

        // ========== HITCIRCLE CONVERSION ==========
        // Compute density BEFORE pattern generation (as per osu!lazer)
        computeDensity(note.time);

        // Determine convertType (from HitCirclePatternGenerator constructor)
        int convertType = None;

        if (timeSep <= 80) {
            convertType |= ForceNotStack | KeepSingle;
        } else if (timeSep <= 95) {
            convertType |= ForceNotStack | KeepSingle | lastStair;
        } else if (timeSep <= 105) {
            convertType |= ForceNotStack | LowProbability;
        } else if (timeSep <= 125) {
            convertType |= ForceNotStack;
        } else if (timeSep <= 135 && posSep < 20) {
            convertType |= Cycle | KeepSingle;
        } else if (timeSep <= 150 && posSep < 20) {
            convertType |= ForceStack | LowProbability;
        } else if (posSep < 20 && density >= beatLength / 2.5) {
            // Low density stream
            convertType |= Reverse | LowProbability;
        } else if (density < beatLength / 2.5) {
            // High density - no flags (Kiai mode also triggers this, but we don't track it)
        } else {
            convertType |= LowProbability;
        }

        // Add Mirror/Gathered flags if not KeepSingle (from HitCirclePatternGenerator)
        if (!(convertType & KeepSingle)) {
            if (note.hasFinish && targetKeyCount != 8) {
                convertType |= Mirror;
            } else if (note.hasClap) {
                convertType |= Gathered;
            }
        }

        // Debug output for HitCircle
        if (i < 60) {
            std::cerr << "  HitCircle convertType=" << convertType
                      << " density=" << density << " beatLength=" << beatLength
                      << " prevCols=" << previousColumns.size() << std::endl;
        }

        bool patternGenerated = false;

        // Generate pattern (following HitCirclePatternGenerator.Generate order)

        // 1. Reverse
        if ((convertType & Reverse) && !previousColumns.empty()) {
            if (i < 60) {
                std::cerr << "  Reverse: prevCols={";
                for (int c : previousColumns) std::cerr << c << ",";
                std::cerr << "} -> {";
            }
            for (int prevCol : previousColumns) {
                column = randomStart + targetKeyCount - prevCol - 1;
                currentColumns.insert(column);
                if (i < 60) std::cerr << column << ",";
            }
            if (i < 60) std::cerr << "}" << std::endl;
            patternGenerated = true;
        }

        // 2. Cycle
        if (!patternGenerated && (convertType & Cycle) && previousColumns.size() == 1) {
            bool canCycle = (targetKeyCount != 8 || lastColumn != 0) &&
                           (targetKeyCount % 2 == 0 || lastColumn != targetKeyCount / 2);
            if (canCycle) {
                column = randomStart + targetKeyCount - lastColumn - 1;
                currentColumns.insert(column);
                patternGenerated = true;
            }
        }

        // 3. ForceStack
        if (!patternGenerated && (convertType & ForceStack) && !previousColumns.empty()) {
            currentColumns = previousColumns;
            patternGenerated = true;
        }

        // 4. Stair (only if previous had 1 note)
        if (!patternGenerated && (convertType & Stair) && previousColumns.size() == 1) {
            column = lastColumn + 1;
            if (column == targetKeyCount) column = randomStart;
            currentColumns.insert(column);
            patternGenerated = true;
        }

        // 5. ReverseStair (only if previous had 1 note)
        if (!patternGenerated && (convertType & ReverseStair) && previousColumns.size() == 1) {
            column = lastColumn - 1;
            if (column == randomStart - 1) column = targetKeyCount - 1;
            currentColumns.insert(column);
            patternGenerated = true;
        }

        // 6. KeepSingle - generate 1 random note (uses generateRandomNotes)
        if (!patternGenerated && (convertType & KeepSingle)) {
            column = getColumn(posX, targetKeyCount, true);
            // generateRandomNotes only avoids current pattern, not previousColumns
            column = findAvailableColumn(column, targetKeyCount, currentColumns, random, randomStart);
            currentColumns.insert(column);
            patternGenerated = true;
        }

        // 7. Mirror - generate mirrored pattern
        if (!patternGenerated && (convertType & Mirror)) {
            // If ForceNotStack, fall through to default pattern generation
            if (convertType & ForceNotStack) {
                // Will be handled by default case below
            } else {
                // Simplified mirror implementation
                double centreProbability, p2, p3;
                if (convDiff > 6.5) { centreProbability = 0.12; p2 = 0.38; p3 = 0.12; }
                else if (convDiff > 4) { centreProbability = 0.12; p2 = 0.17; p3 = 0; }
                else { centreProbability = 0.12; p2 = 0; p3 = 0; }

                // Adjust by TotalColumns (from getRandomNoteCountMirrored)
                if (targetKeyCount == 2) { centreProbability = 0; p2 = 0; p3 = 0; }
                else if (targetKeyCount == 3) { centreProbability = std::min(centreProbability, 0.03); p2 = 0; p3 = 0; }
                else if (targetKeyCount == 4) { centreProbability = 0; p2 = 1 - std::max((1 - p2) * 2, 0.8); p3 = 0; }
                else if (targetKeyCount == 5) { centreProbability = std::min(centreProbability, 0.03); p3 = 0; }
                else if (targetKeyCount == 6) { centreProbability = 0; p2 = 1 - std::max((1 - p2) * 2, 0.5); p3 = 1 - std::max((1 - p3) * 2, 0.85); }

                p2 = std::clamp(p2, 0.0, 1.0);
                p3 = std::clamp(p3, 0.0, 1.0);

                double centreVal = random.nextDouble();
                int noteCount = getRandomNoteCount(p2, p3, 0, 0, random);
                bool addToCentre = (targetKeyCount % 2 != 0) && (noteCount != 3) && (centreVal > 1 - centreProbability);

                int columnLimit = (targetKeyCount % 2 == 0 ? targetKeyCount : targetKeyCount - 1) / 2;
                int nextColumn = random.next(randomStart, columnLimit);

                for (int n = 0; n < noteCount; n++) {
                    nextColumn = findAvailableColumn(nextColumn, columnLimit, currentColumns, random, randomStart);
                    currentColumns.insert(nextColumn);
                    // Add mirrored note
                    int mirroredCol = randomStart + targetKeyCount - nextColumn - 1;
                    currentColumns.insert(mirroredCol);
                }

                if (addToCentre) {
                    currentColumns.insert(targetKeyCount / 2);
                }

                patternGenerated = true;
            }
        }

        // 8. Default - generateRandomPattern based on ConversionDifficulty
        if (!patternGenerated) {
            double p2, p3, p4, p5;
            bool lowProb = (convertType & LowProbability) != 0;

            if (convDiff > 6.5) {
                if (lowProb) { p2 = 0.78; p3 = 0.42; p4 = 0; p5 = 0; }
                else { p2 = 1; p3 = 0.62; p4 = 0; p5 = 0; }
            } else if (convDiff > 4) {
                if (lowProb) { p2 = 0.35; p3 = 0.08; p4 = 0; p5 = 0; }
                else { p2 = 0.52; p3 = 0.15; p4 = 0; p5 = 0; }
            } else if (convDiff > 2) {
                if (lowProb) { p2 = 0.18; p3 = 0; p4 = 0; p5 = 0; }
                else { p2 = 0.45; p3 = 0; p4 = 0; p5 = 0; }
            } else {
                p2 = 0; p3 = 0; p4 = 0; p5 = 0;
            }

            // Adjust probabilities based on TotalColumns (from HitCirclePatternGenerator)
            if (targetKeyCount == 2) {
                p2 = 0; p3 = 0; p4 = 0; p5 = 0;
            } else if (targetKeyCount == 3) {
                p2 = std::min(p2, 0.1); p3 = 0; p4 = 0; p5 = 0;
            } else if (targetKeyCount == 4) {
                p2 = std::min(p2, 0.23); p3 = std::min(p3, 0.04); p4 = 0; p5 = 0;
            } else if (targetKeyCount == 5) {
                p3 = std::min(p3, 0.15); p4 = std::min(p4, 0.03); p5 = 0;
            }

            // If has clap sound, p2 = 1 (from HitCirclePatternGenerator.getRandomNoteCount)
            if (note.hasClap) {
                p2 = 1;
            }

            int noteCount = getRandomNoteCount(p2, p3, p4, p5, random);

            // Debug output
            if (i < 60) {
                std::cerr << "  Default: p2=" << p2 << " noteCount=" << noteCount;
            }

            // generateRandomNotes logic
            column = getColumn(posX, targetKeyCount, true);
            bool allowStacking = !(convertType & ForceNotStack);

            // Limit noteCount if not allowing stacking
            if (!allowStacking) {
                int maxNotes = targetKeyCount - randomStart - (int)previousColumns.size();
                if (i < 60) {
                    std::cerr << " maxNotes=" << maxNotes;
                }
                noteCount = std::min(noteCount, std::max(0, maxNotes));
            }
            if (i < 60) {
                std::cerr << " finalNoteCount=" << noteCount << std::endl;
            }

            for (int n = 0; n < noteCount; n++) {
                // Build blocked set based on allowStacking
                std::set<int> blocked = currentColumns;
                if (!allowStacking) {
                    for (int c : previousColumns) blocked.insert(c);
                }

                // Gathered mode: use sequential columns instead of random
                if (convertType & Gathered) {
                    // First check if initial column is valid (like osu!lazer)
                    if (blocked.find(column) == blocked.end()) {
                        // Initial column is valid, use it
                    } else {
                        // Find next available column sequentially
                        int startCol = column;
                        do {
                            column++;
                            if (column == targetKeyCount) column = randomStart;
                        } while (blocked.find(column) != blocked.end() && column != startCol);
                    }
                } else {
                    column = findAvailableColumn(column, targetKeyCount, blocked, random, randomStart);
                }
                currentColumns.insert(column);
            }
        }

        // Debug: output final columns
        if (i < 60 && note.objectType == ObjectType::HitCircle) {
            std::cerr << "  Result: {";
            for (int c : currentColumns) std::cerr << (c+1) << ",";
            std::cerr << "}" << std::endl;
        }

        // Create notes from currentColumns
        for (int col : currentColumns) {
            Note newNote(col, note.time, note.isHold, note.endTime);
            newNote.x = note.x;
            convertedNotes.push_back(newNote);
        }

        // Update state for next iteration
        if (!currentColumns.empty()) {
            lastColumn = *currentColumns.rbegin();
            previousColumns = currentColumns;
        }
        // Record note AFTER pattern generation (as per osu!lazer)
        recordNote(note.time, posX);

        // Toggle stair direction only when reaching boundary
        for (int col : currentColumns) {
            if ((convertType & Stair) && col == targetKeyCount - 1) {
                lastStair = ReverseStair;
            }
            if ((convertType & ReverseStair) && col == randomStart) {
                lastStair = Stair;
            }
        }
    }

    // Replace original notes with converted notes
    beatmap.notes = std::move(convertedNotes);
    beatmap.keyCount = targetKeyCount;
    beatmap.mode = 3;

    // Sort by time
    std::sort(beatmap.notes.begin(), beatmap.notes.end(),
        [](const Note& a, const Note& b) { return a.time < b.time; });

    return true;
}