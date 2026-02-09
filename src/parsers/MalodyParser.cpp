#include "MalodyParser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

// Simple JSON parsing helpers (Malody uses simple JSON structure)
namespace {

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Extract string value from JSON: "key":"value" or "key": "value"
std::string getJsonString(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";

    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";

    size_t endPos = json.find('"', pos + 1);
    if (endPos == std::string::npos) return "";

    return json.substr(pos + 1, endPos - pos - 1);
}

// Extract number value from JSON: "key":123 or "key": 123.5
double getJsonNumber(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return 0.0;

    pos = json.find(':', pos);
    if (pos == std::string::npos) return 0.0;

    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    size_t endPos = pos;
    while (endPos < json.size() && (isdigit(json[endPos]) || json[endPos] == '.' || json[endPos] == '-')) {
        endPos++;
    }

    if (endPos == pos) return 0.0;
    return std::stod(json.substr(pos, endPos - pos));
}

// Extract integer value
int getJsonInt(const std::string& json, const std::string& key) {
    return static_cast<int>(getJsonNumber(json, key));
}

} // anonymous namespace

double MalodyParser::getBpmAtBeat(double beatPos, double baseBpm,
                                   const std::vector<std::pair<double, double>>& bpmChanges) {
    double currentBpm = baseBpm;
    for (const auto& change : bpmChanges) {
        if (change.first <= beatPos) {
            currentBpm = change.second;
        } else {
            break;
        }
    }
    return currentBpm;
}

int64_t MalodyParser::beatToMs(int measure, int numerator, int denominator,
                                double baseBpm,
                                const std::vector<std::pair<double, double>>& bpmChanges) {
    // Calculate beat position: measure + numerator/denominator
    // In Malody, beat[0] is the beat number (not measure), beat[1]/beat[2] is the fraction
    double beatPos = measure + (denominator > 0 ? static_cast<double>(numerator) / denominator : 0.0);

    // If no BPM changes, simple calculation
    // time(ms) = beat * (60000 / bpm)
    if (bpmChanges.empty()) {
        return static_cast<int64_t>(beatPos * 60000.0 / baseBpm);
    }

    // With BPM changes, calculate time segment by segment
    double totalMs = 0.0;
    double currentBeat = 0.0;
    double currentBpm = baseBpm;

    for (const auto& change : bpmChanges) {
        double changeBeat = change.first;
        if (changeBeat >= beatPos) break;

        // Add time from currentBeat to changeBeat at currentBpm
        double beatDiff = changeBeat - currentBeat;
        totalMs += beatDiff * 60000.0 / currentBpm;

        currentBeat = changeBeat;
        currentBpm = change.second;
    }

    // Add remaining time from currentBeat to beatPos
    double remainingBeats = beatPos - currentBeat;
    totalMs += remainingBeats * 60000.0 / currentBpm;

    return static_cast<int64_t>(totalMs);
}

bool MalodyParser::parse(const std::string& path, BeatmapInfo& info) {
    // Read entire file
    std::ifstream file(path);
    if (!file) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();
    file.close();

    // Verify it's a valid Malody file (should start with {)
    std::string trimmed = trim(json);
    if (trimmed.empty() || trimmed[0] != '{') return false;

    // Extract meta section
    size_t metaPos = json.find("\"meta\"");
    if (metaPos == std::string::npos) return false;

    // Find meta object boundaries
    size_t metaStart = json.find('{', metaPos);
    if (metaStart == std::string::npos) return false;

    int braceCount = 1;
    size_t metaEnd = metaStart + 1;
    while (metaEnd < json.size() && braceCount > 0) {
        if (json[metaEnd] == '{') braceCount++;
        else if (json[metaEnd] == '}') braceCount--;
        metaEnd++;
    }
    std::string metaJson = json.substr(metaStart, metaEnd - metaStart);

    // Check mode (only Key mode = 0 is supported)
    int mode = getJsonInt(metaJson, "mode");
    if (mode != 0) {
        // Not Key mode, unsupported
        return false;
    }

    // Extract metadata
    info.creator = getJsonString(metaJson, "creator");
    info.version = getJsonString(metaJson, "version");

    // Extract song info
    size_t songPos = metaJson.find("\"song\"");
    if (songPos != std::string::npos) {
        size_t songStart = metaJson.find('{', songPos);
        if (songStart != std::string::npos) {
            size_t songEnd = metaJson.find('}', songStart);
            std::string songJson = metaJson.substr(songStart, songEnd - songStart + 1);
            info.title = getJsonString(songJson, "title");
            info.artist = getJsonString(songJson, "artist");
        }
    }

    // Extract key count from mode_ext
    size_t modeExtPos = metaJson.find("\"mode_ext\"");
    if (modeExtPos != std::string::npos) {
        size_t modeExtStart = metaJson.find('{', modeExtPos);
        if (modeExtStart != std::string::npos) {
            size_t modeExtEnd = metaJson.find('}', modeExtStart);
            std::string modeExtJson = metaJson.substr(modeExtStart, modeExtEnd - modeExtStart + 1);
            info.keyCount = getJsonInt(modeExtJson, "column");
        }
    }
    if (info.keyCount < 1 || info.keyCount > 10) {
        info.keyCount = 4;  // Default to 4K
    }

    info.mode = 3;  // osu!mania mode
    info.od = 8.0f;  // Default OD8 for Malody

    // Parse time array (BPM changes)
    // Note: search after metaEnd to avoid finding "time" timestamp in meta section
    std::vector<std::pair<double, double>> bpmChanges;  // {beatPos, bpm}
    double baseBpm = 120.0;

    std::cout << "[Malody Debug] metaEnd=" << metaEnd << std::endl;
    size_t timePos = json.find("\"time\"", metaEnd);
    std::cout << "[Malody Debug] timePos=" << timePos << std::endl;
    if (timePos != std::string::npos) {
        size_t timeStart = json.find('[', timePos);
        if (timeStart != std::string::npos) {
            // Find matching closing bracket
            int bracketCount = 1;
            size_t timeEnd = timeStart + 1;
            while (timeEnd < json.size() && bracketCount > 0) {
                if (json[timeEnd] == '[') bracketCount++;
                else if (json[timeEnd] == ']') bracketCount--;
                timeEnd++;
            }
            std::cout << "[Malody Debug] timeStart=" << timeStart << ", timeEnd=" << timeEnd << std::endl;
            std::string timeJson = json.substr(timeStart, timeEnd - timeStart);
            std::cout << "[Malody Debug] timeJson=" << timeJson << std::endl;

            // Parse each time entry
            size_t pos = 0;
            while ((pos = timeJson.find("{", pos)) != std::string::npos) {
                size_t entryEnd = timeJson.find("}", pos);
                if (entryEnd == std::string::npos) break;

                std::string entry = timeJson.substr(pos, entryEnd - pos + 1);

                // Parse beat array
                size_t beatPos = entry.find("\"beat\"");
                int measure = 0, numerator = 0, denominator = 1;
                if (beatPos != std::string::npos) {
                    size_t arrStart = entry.find('[', beatPos);
                    size_t arrEnd = entry.find(']', arrStart);
                    if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                        std::string beatArr = entry.substr(arrStart + 1, arrEnd - arrStart - 1);
                        sscanf(beatArr.c_str(), "%d,%d,%d", &measure, &numerator, &denominator);
                    }
                }

                double bpm = getJsonNumber(entry, "bpm");
                if (bpm > 0) {
                    double beatPosition = measure + (denominator > 0 ? (double)numerator / denominator : 0.0);
                    bpmChanges.push_back({beatPosition, bpm});
                    if (bpmChanges.size() == 1) {
                        baseBpm = bpm;
                    }
                }

                pos = entryEnd + 1;
            }
        }
    }

    // Sort BPM changes by beat position
    std::sort(bpmChanges.begin(), bpmChanges.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    // Parse effect array (SV changes)
    std::vector<std::pair<double, double>> svChanges;  // {beatPos, scroll}
    size_t effectPos = json.find("\"effect\"");
    if (effectPos != std::string::npos) {
        size_t effectStart = json.find('[', effectPos);
        if (effectStart != std::string::npos) {
            // Find matching closing bracket
            int bracketCount = 1;
            size_t effectEnd = effectStart + 1;
            while (effectEnd < json.size() && bracketCount > 0) {
                if (json[effectEnd] == '[') bracketCount++;
                else if (json[effectEnd] == ']') bracketCount--;
                effectEnd++;
            }
            std::string effectJson = json.substr(effectStart, effectEnd - effectStart);

            // Parse each effect entry
            size_t pos = 0;
            while ((pos = effectJson.find("{", pos)) != std::string::npos) {
                size_t entryEnd = effectJson.find("}", pos);
                if (entryEnd == std::string::npos) break;

                std::string entry = effectJson.substr(pos, entryEnd - pos + 1);

                // Parse beat array
                size_t beatPos = entry.find("\"beat\"");
                int measure = 0, numerator = 0, denominator = 1;
                if (beatPos != std::string::npos) {
                    size_t arrStart = entry.find('[', beatPos);
                    size_t arrEnd = entry.find(']', arrStart);
                    if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                        std::string beatArr = entry.substr(arrStart + 1, arrEnd - arrStart - 1);
                        sscanf(beatArr.c_str(), "%d,%d,%d", &measure, &numerator, &denominator);
                    }
                }

                double scroll = getJsonNumber(entry, "scroll");
                if (scroll != 0.0) {
                    double beatPosition = measure + (denominator > 0 ? (double)numerator / denominator : 0.0);
                    svChanges.push_back({beatPosition, scroll});
                }

                pos = entryEnd + 1;
            }
        }
    }

    // Parse note array
    size_t notePos = json.find("\"note\"");
    if (notePos == std::string::npos) return false;

    size_t noteStart = json.find('[', notePos);
    if (noteStart == std::string::npos) return false;

    // Find matching closing bracket for note array
    int bracketCount = 1;
    size_t noteEnd = noteStart + 1;
    while (noteEnd < json.size() && bracketCount > 0) {
        if (json[noteEnd] == '[') bracketCount++;
        else if (json[noteEnd] == ']') bracketCount--;
        noteEnd++;
    }
    if (bracketCount != 0) return false;

    std::string noteJson = json.substr(noteStart, noteEnd - noteStart);

    // First pass: find BGM entry and extract offset
    // In Malody, the BGM note has "sound" field and optional "offset" field
    double audioOffset = 0.0;
    size_t pos = 0;
    while ((pos = noteJson.find("{", pos)) != std::string::npos) {
        size_t entryEnd = noteJson.find("}", pos);
        if (entryEnd == std::string::npos) break;

        std::string entry = noteJson.substr(pos, entryEnd - pos + 1);

        // Check if this is a sound/BGM entry (has "sound" field)
        if (entry.find("\"sound\"") != std::string::npos) {
            info.audioFilename = getJsonString(entry, "sound");
            // Extract offset - this is the audio offset in milliseconds
            audioOffset = getJsonNumber(entry, "offset");
            break;
        }
        pos = entryEnd + 1;
    }

    // Second pass: parse all notes
    pos = 0;
    while ((pos = noteJson.find("{", pos)) != std::string::npos) {
        size_t entryEnd = noteJson.find("}", pos);
        if (entryEnd == std::string::npos) break;

        std::string entry = noteJson.substr(pos, entryEnd - pos + 1);

        // Skip BGM entry
        if (entry.find("\"sound\"") != std::string::npos) {
            pos = entryEnd + 1;
            continue;
        }

        // Check if this has a column (actual note)
        if (entry.find("\"column\"") == std::string::npos) {
            pos = entryEnd + 1;
            continue;
        }

        // Parse beat array for note start time
        int measure = 0, numerator = 0, denominator = 1;
        size_t beatPos = entry.find("\"beat\"");
        if (beatPos != std::string::npos) {
            size_t arrStart = entry.find('[', beatPos);
            size_t arrEnd = entry.find(']', arrStart);
            if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                std::string beatArr = entry.substr(arrStart + 1, arrEnd - arrStart - 1);
                sscanf(beatArr.c_str(), "%d,%d,%d", &measure, &numerator, &denominator);
            }
        }

        int column = getJsonInt(entry, "column");
        // Calculate time and subtract audio offset (malody2osu uses -offset as initial offset)
        int64_t startTime = beatToMs(measure, numerator, denominator, baseBpm, bpmChanges) - static_cast<int64_t>(audioOffset);

        // Check for hold note (has "endbeat" field)
        bool isHold = false;
        int64_t endTime = 0;
        size_t endBeatPos = entry.find("\"endbeat\"");
        if (endBeatPos != std::string::npos) {
            int endMeasure = 0, endNumerator = 0, endDenominator = 1;
            size_t endArrStart = entry.find('[', endBeatPos);
            size_t endArrEnd = entry.find(']', endArrStart);
            if (endArrStart != std::string::npos && endArrEnd != std::string::npos) {
                std::string endBeatArr = entry.substr(endArrStart + 1, endArrEnd - endArrStart - 1);
                sscanf(endBeatArr.c_str(), "%d,%d,%d", &endMeasure, &endNumerator, &endDenominator);
                endTime = beatToMs(endMeasure, endNumerator, endDenominator, baseBpm, bpmChanges) - static_cast<int64_t>(audioOffset) - 2500;
                isHold = (endTime > startTime);
            }
        }

        // Validate column
        if (column < 0 || column >= info.keyCount) {
            pos = entryEnd + 1;
            continue;
        }

        // Create note
        Note note(column, startTime, isHold, isHold ? endTime : 0);
        info.notes.push_back(note);

        pos = entryEnd + 1;
    }

    // Sort notes by time
    std::sort(info.notes.begin(), info.notes.end(),
        [](const Note& a, const Note& b) { return a.time < b.time; });

    // Create timing points from BPM changes
    if (!bpmChanges.empty()) {
        for (const auto& change : bpmChanges) {
            TimingPoint tp;
            tp.time = beatToMs(static_cast<int>(change.first),
                              static_cast<int>((change.first - static_cast<int>(change.first)) * 1000) % 1000,
                              1000, baseBpm, {});
            tp.beatLength = 60000.0 / change.second;
            tp.uninherited = true;
            tp.effectiveBeatLength = tp.beatLength;
            tp.volume = 100;
            info.timingPoints.push_back(tp);
        }
    } else {
        // Add default timing point
        TimingPoint tp;
        tp.time = 0;
        tp.beatLength = 60000.0 / baseBpm;
        tp.uninherited = true;
        tp.effectiveBeatLength = tp.beatLength;
        tp.volume = 100;
        info.timingPoints.push_back(tp);
    }

    // Add SV timing points (inherited, uninherited=false)
    for (const auto& sv : svChanges) {
        TimingPoint tp;
        tp.time = beatToMs(static_cast<int>(sv.first),
                          static_cast<int>((sv.first - static_cast<int>(sv.first)) * 1000) % 1000,
                          1000, baseBpm, bpmChanges) - static_cast<int64_t>(audioOffset);
        // In osu!, inherited timing point beatLength is -100/scroll
        // scroll=0 means stop (use very small SV), negative scroll means reverse (use abs)
        if (sv.second == 0.0) {
            tp.beatLength = -0.01;  // SV = 0.001, effectively stops notes
        } else {
            // Use absolute value to ensure beatLength is always negative (valid green line)
            // Clamp extreme values to prevent notes from flying off screen
            double absScroll = std::abs(sv.second);
            absScroll = std::max(0.1, std::min(absScroll, 10.0));  // Clamp to 0.1x - 10x
            tp.beatLength = -100.0 / absScroll;
        }
        tp.uninherited = false;
        tp.effectiveBeatLength = tp.beatLength;
        tp.volume = 100;
        info.timingPoints.push_back(tp);
    }

    // Sort timing points by time
    std::sort(info.timingPoints.begin(), info.timingPoints.end(),
        [](const TimingPoint& a, const TimingPoint& b) { return a.time < b.time; });

    // Calculate MD5 hash
    info.beatmapHash = OsuParser::calculateMD5(path);

    // Debug output
    std::cout << "[Malody Debug] baseBpm=" << baseBpm << std::endl;
    std::cout << "[Malody Debug] bpmChanges.size()=" << bpmChanges.size() << std::endl;
    if (!bpmChanges.empty()) {
        std::cout << "[Malody Debug] bpmChanges[0]=(" << bpmChanges[0].first << ", " << bpmChanges[0].second << ")" << std::endl;
    }
    std::cout << "[Malody Debug] notes.size()=" << info.notes.size() << std::endl;
    if (!info.notes.empty()) {
        std::cout << "[Malody Debug] firstNote time=" << info.notes.front().time << "ms" << std::endl;
        std::cout << "[Malody Debug] lastNote time=" << info.notes.back().time << "ms" << std::endl;
    }

    return true;
}