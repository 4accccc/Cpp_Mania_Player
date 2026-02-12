#include "StepManiaParser.h"
#include "OsuParser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <regex>

bool StepManiaParser::isStepManiaFile(const std::string& path) {
    size_t dotPos = path.rfind('.');
    if (dotPos == std::string::npos) return false;
    std::string ext = path.substr(dotPos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".sm" || ext == ".ssc";
}

std::string StepManiaParser::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::string StepManiaParser::getTagValue(const std::string& content, const std::string& tag) {
    std::string searchTag = "#" + tag + ":";
    size_t pos = content.find(searchTag);
    if (pos == std::string::npos) return "";

    pos += searchTag.length();
    size_t endPos = content.find(';', pos);
    if (endPos == std::string::npos) return "";

    return trim(content.substr(pos, endPos - pos));
}

int StepManiaParser::stepsTypeToKeyCount(const std::string& stepsType) {
    std::string type = stepsType;
    std::transform(type.begin(), type.end(), type.begin(), ::tolower);
    // Remove leading/trailing whitespace
    type = trim(type);

    if (type == "dance-single") return 4;
    if (type == "dance-double") return 8;
    if (type == "dance-couple") return 8;
    if (type == "dance-solo") return 6;
    if (type == "pump-single") return 5;
    if (type == "pump-double") return 10;
    if (type == "pump-halfdouble") return 6;
    if (type == "kb7-single") return 7;
    if (type == "ez2-single") return 5;
    if (type == "ez2-double") return 10;
    if (type == "para-single") return 5;
    if (type == "ds3ddx-single") return 8;
    if (type == "bm-single") return 6;   // beatmania style
    if (type == "bm-double") return 12;
    if (type == "maniax-single") return 4;
    if (type == "maniax-double") return 8;
    if (type == "techno-single4") return 4;
    if (type == "techno-single5") return 5;
    if (type == "techno-single8") return 8;
    if (type == "techno-double4") return 8;
    if (type == "techno-double5") return 10;
    if (type == "pnm-five") return 5;
    if (type == "pnm-nine") return 9;

    // Default: try to detect from name
    if (type.find("double") != std::string::npos) return 8;
    if (type.find("single") != std::string::npos) return 4;

    return 4; // fallback
}

std::vector<StepManiaParser::BPMChange> StepManiaParser::parseBPMs(const std::string& bpmStr) {
    std::vector<BPMChange> bpms;
    if (bpmStr.empty()) return bpms;

    std::stringstream ss(bpmStr);
    std::string segment;

    while (std::getline(ss, segment, ',')) {
        segment = trim(segment);
        if (segment.empty()) continue;

        size_t eqPos = segment.find('=');
        if (eqPos == std::string::npos) continue;

        try {
            BPMChange change;
            change.beat = std::stod(segment.substr(0, eqPos));
            change.bpm = std::stod(segment.substr(eqPos + 1));
            if (change.bpm > 0) {
                bpms.push_back(change);
            }
        } catch (...) {
            continue;
        }
    }

    // Sort by beat
    std::sort(bpms.begin(), bpms.end(), [](const BPMChange& a, const BPMChange& b) {
        return a.beat < b.beat;
    });

    return bpms;
}

std::vector<StepManiaParser::StopPoint> StepManiaParser::parseStops(const std::string& stopStr) {
    std::vector<StopPoint> stops;
    if (stopStr.empty()) return stops;

    std::stringstream ss(stopStr);
    std::string segment;

    while (std::getline(ss, segment, ',')) {
        segment = trim(segment);
        if (segment.empty()) continue;

        size_t eqPos = segment.find('=');
        if (eqPos == std::string::npos) continue;

        try {
            StopPoint stop;
            stop.beat = std::stod(segment.substr(0, eqPos));
            stop.duration = std::stod(segment.substr(eqPos + 1));
            if (stop.duration > 0) {
                stops.push_back(stop);
            }
        } catch (...) {
            continue;
        }
    }

    std::sort(stops.begin(), stops.end(), [](const StopPoint& a, const StopPoint& b) {
        return a.beat < b.beat;
    });

    return stops;
}

double StepManiaParser::beatToMs(double beat, const std::vector<BPMChange>& bpms,
                                  const std::vector<StopPoint>& stops, double offset) {
    if (bpms.empty()) return 0;

    double totalMs = -offset * 1000.0;  // offset is in seconds, negative means delay
    double currentBeat = 0.0;
    double currentBpm = bpms[0].bpm;
    size_t bpmIdx = 0;

    while (currentBeat < beat) {
        // Find next BPM change
        double nextBpmBeat = (bpmIdx + 1 < bpms.size()) ? bpms[bpmIdx + 1].beat : beat + 1;
        double targetBeat = std::min(beat, nextBpmBeat);

        // Calculate time for this segment
        double beatsInSegment = targetBeat - currentBeat;
        double msPerBeat = 60000.0 / currentBpm;
        totalMs += beatsInSegment * msPerBeat;

        currentBeat = targetBeat;

        // Move to next BPM if we reached it
        if (bpmIdx + 1 < bpms.size() && currentBeat >= bpms[bpmIdx + 1].beat) {
            bpmIdx++;
            currentBpm = bpms[bpmIdx].bpm;
        }
    }

    // Add stop durations for stops before this beat
    for (const auto& stop : stops) {
        if (stop.beat <= beat) {
            totalMs += stop.duration * 1000.0;
        }
    }

    return totalMs;
}

bool StepManiaParser::parseNoteData(const std::string& noteData, int keyCount,
                                     const std::vector<BPMChange>& bpms,
                                     const std::vector<StopPoint>& stops,
                                     double offset,
                                     std::vector<Note>& notes) {
    // Track hold note start times for each lane
    std::vector<int64_t> holdStartTime(keyCount, -1);
    std::vector<int> holdStartIdx(keyCount, -1);

    // Split into measures by comma
    std::vector<std::string> measures;
    std::stringstream ss(noteData);
    std::string measure;

    while (std::getline(ss, measure, ',')) {
        measures.push_back(measure);
    }

    int measureNum = 0;
    for (const auto& measureStr : measures) {
        // Split measure into rows
        std::vector<std::string> rows;
        std::stringstream rowSs(measureStr);
        std::string row;

        while (std::getline(rowSs, row)) {
            row = trim(row);
            // Skip empty lines and comments
            if (row.empty() || row[0] == '/' || row[0] == '#') continue;
            // Must have correct number of characters
            if ((int)row.length() >= keyCount) {
                rows.push_back(row.substr(0, keyCount));
            }
        }

        if (rows.empty()) {
            measureNum++;
            continue;
        }

        int rowsInMeasure = (int)rows.size();
        for (int rowIdx = 0; rowIdx < rowsInMeasure; rowIdx++) {
            const std::string& rowData = rows[rowIdx];

            // Calculate beat: measure * 4 + (rowIdx / rowsInMeasure) * 4
            double beat = measureNum * 4.0 + (rowIdx / (double)rowsInMeasure) * 4.0;
            int64_t timeMs = static_cast<int64_t>(beatToMs(beat, bpms, stops, offset));

            for (int lane = 0; lane < keyCount && lane < (int)rowData.length(); lane++) {
                char noteType = rowData[lane];

                switch (noteType) {
                    case '1': // Tap note
                    case 'L': // Lift (treat as tap)
                    {
                        Note note(lane, timeMs, false, 0);
                        notes.push_back(note);
                        break;
                    }
                    case '2': // Hold head
                    case '4': // Roll head (treat as hold)
                    {
                        holdStartTime[lane] = timeMs;
                        holdStartIdx[lane] = (int)notes.size();
                        Note note(lane, timeMs, true, timeMs); // endTime will be updated
                        notes.push_back(note);
                        break;
                    }
                    case '3': // Hold/Roll tail
                    {
                        if (holdStartIdx[lane] >= 0 && holdStartIdx[lane] < (int)notes.size()) {
                            notes[holdStartIdx[lane]].endTime = timeMs;
                        }
                        holdStartIdx[lane] = -1;
                        holdStartTime[lane] = -1;
                        break;
                    }
                    case 'M': // Mine - ignore
                    case 'F': // Fake - ignore
                    case '0': // Empty
                    default:
                        break;
                }
            }
        }
        measureNum++;
    }

    return true;
}

bool StepManiaParser::parseSM(const std::string& content, const std::string& path,
                               BeatmapInfo& info, int difficultyIndex) {
    // Parse metadata
    info.title = getTagValue(content, "TITLE");
    info.artist = getTagValue(content, "ARTIST");
    info.creator = getTagValue(content, "CREDIT");
    info.audioFilename = getTagValue(content, "MUSIC");

    // Parse preview time (SAMPLESTART is in seconds)
    std::string sampleStartStr = getTagValue(content, "SAMPLESTART");
    if (!sampleStartStr.empty()) {
        try {
            info.previewTime = static_cast<int>(std::stod(sampleStartStr) * 1000.0);
        } catch (...) {
            info.previewTime = -1;
        }
    }

    std::string offsetStr = getTagValue(content, "OFFSET");
    double offset = offsetStr.empty() ? 0.0 : std::stod(offsetStr);

    auto bpms = parseBPMs(getTagValue(content, "BPMS"));
    auto stops = parseStops(getTagValue(content, "STOPS"));

    // Find all #NOTES: sections
    std::vector<size_t> notesPositions;
    size_t searchPos = 0;
    while ((searchPos = content.find("#NOTES:", searchPos)) != std::string::npos) {
        notesPositions.push_back(searchPos);
        searchPos += 7;
    }

    if (notesPositions.empty()) return false;

    // Select difficulty
    int targetIdx = (difficultyIndex >= 0 && difficultyIndex < (int)notesPositions.size())
                    ? difficultyIndex : 0;

    size_t notesStart = notesPositions[targetIdx] + 7;
    size_t notesEnd = content.find(';', notesStart);
    if (notesEnd == std::string::npos) return false;

    std::string notesSection = content.substr(notesStart, notesEnd - notesStart);

    // SM format: stepstype:description:difficulty:meter:radarvalues:notedata
    // Split by colon, but note data contains newlines
    std::vector<std::string> parts;
    std::stringstream ss(notesSection);
    std::string part;
    int colonCount = 0;

    while (colonCount < 5 && std::getline(ss, part, ':')) {
        parts.push_back(trim(part));
        colonCount++;
    }

    if (parts.size() < 5) return false;

    std::string stepsType = parts[0];
    std::string difficulty = parts[2];
    std::string meterStr = parts[3];

    // Get note data (everything after the 5th colon)
    size_t noteDataStart = notesStart;
    for (int i = 0; i < 5; i++) {
        noteDataStart = content.find(':', noteDataStart) + 1;
    }
    std::string noteData = content.substr(noteDataStart, notesEnd - noteDataStart);

    int keyCount = stepsTypeToKeyCount(stepsType);
    info.keyCount = keyCount;
    info.version = difficulty;
    info.mode = 3; // mania mode

    try {
        info.od = std::stof(meterStr);
    } catch (...) {
        info.od = 5.0f;
    }

    return parseNoteData(noteData, keyCount, bpms, stops, offset, info.notes);
}

bool StepManiaParser::parseSSC(const std::string& content, const std::string& path,
                                BeatmapInfo& info, int difficultyIndex) {
    // Parse global metadata
    info.title = getTagValue(content, "TITLE");
    info.artist = getTagValue(content, "ARTIST");
    info.creator = getTagValue(content, "CREDIT");
    info.audioFilename = getTagValue(content, "MUSIC");

    // Parse preview time (SAMPLESTART is in seconds)
    std::string sampleStartStr = getTagValue(content, "SAMPLESTART");
    if (!sampleStartStr.empty()) {
        try {
            info.previewTime = static_cast<int>(std::stod(sampleStartStr) * 1000.0);
        } catch (...) {
            info.previewTime = -1;
        }
    }

    std::string offsetStr = getTagValue(content, "OFFSET");
    double offset = offsetStr.empty() ? 0.0 : std::stod(offsetStr);

    auto bpms = parseBPMs(getTagValue(content, "BPMS"));
    auto stops = parseStops(getTagValue(content, "STOPS"));

    // Find all #NOTEDATA: sections
    std::vector<size_t> noteDataPositions;
    size_t searchPos = 0;
    while ((searchPos = content.find("#NOTEDATA:", searchPos)) != std::string::npos) {
        noteDataPositions.push_back(searchPos);
        searchPos += 10;
    }

    if (noteDataPositions.empty()) return false;

    // Select difficulty
    int targetIdx = (difficultyIndex >= 0 && difficultyIndex < (int)noteDataPositions.size())
                    ? difficultyIndex : 0;

    size_t sectionStart = noteDataPositions[targetIdx];
    size_t sectionEnd = (targetIdx + 1 < (int)noteDataPositions.size())
                        ? noteDataPositions[targetIdx + 1]
                        : content.length();

    std::string section = content.substr(sectionStart, sectionEnd - sectionStart);

    // Parse section-specific tags
    std::string stepsType = getTagValue(section, "STEPSTYPE");
    std::string difficulty = getTagValue(section, "DIFFICULTY");
    std::string meterStr = getTagValue(section, "METER");

    // Find #NOTES: in this section
    size_t notesPos = section.find("#NOTES:");
    if (notesPos == std::string::npos) return false;

    size_t noteDataStart = notesPos + 7;
    size_t noteDataEnd = section.find(';', noteDataStart);
    if (noteDataEnd == std::string::npos) noteDataEnd = section.length();

    std::string noteData = section.substr(noteDataStart, noteDataEnd - noteDataStart);

    int keyCount = stepsTypeToKeyCount(stepsType);
    info.keyCount = keyCount;
    info.version = difficulty;
    info.mode = 3;

    try {
        info.od = std::stof(meterStr);
    } catch (...) {
        info.od = 5.0f;
    }

    return parseNoteData(noteData, keyCount, bpms, stops, offset, info.notes);
}

bool StepManiaParser::parse(const std::string& path, BeatmapInfo& info, int difficultyIndex) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    bool isSSC = false;
    size_t dotPos = path.rfind('.');
    if (dotPos != std::string::npos) {
        std::string ext = path.substr(dotPos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        isSSC = (ext == ".ssc");
    }
    if (content.find("#VERSION:") != std::string::npos) {
        isSSC = true;
    }

    bool success = isSSC ? parseSSC(content, path, info, difficultyIndex)
                         : parseSM(content, path, info, difficultyIndex);

    if (success) {
        std::sort(info.notes.begin(), info.notes.end(),
            [](const Note& a, const Note& b) { return a.time < b.time; });
        info.beatmapHash = OsuParser::calculateMD5(path);
        if (info.hp == 0) info.hp = 5.0f;
        if (info.od == 0) info.od = 5.0f;
    }
    return success;
}

std::vector<StepManiaParser::DifficultyInfo> StepManiaParser::getDifficulties(const std::string& path) {
    std::vector<DifficultyInfo> diffs;

    std::ifstream file(path);
    if (!file.is_open()) return diffs;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    bool isSSC = (content.find("#VERSION:") != std::string::npos);

    if (isSSC) {
        // SSC format
        size_t pos = 0;
        while ((pos = content.find("#NOTEDATA:", pos)) != std::string::npos) {
            size_t end = content.find("#NOTEDATA:", pos + 10);
            if (end == std::string::npos) end = content.length();
            std::string section = content.substr(pos, end - pos);

            DifficultyInfo info;
            info.stepsType = getTagValue(section, "STEPSTYPE");
            info.difficulty = getTagValue(section, "DIFFICULTY");
            try {
                info.meter = std::stoi(getTagValue(section, "METER"));
            } catch (...) {
                info.meter = 1;
            }
            info.keyCount = stepsTypeToKeyCount(info.stepsType);
            diffs.push_back(info);
            pos += 10;
        }
    } else {
        // SM format
        size_t pos = 0;
        while ((pos = content.find("#NOTES:", pos)) != std::string::npos) {
            pos += 7;
            size_t end = content.find(';', pos);
            if (end == std::string::npos) break;

            std::string section = content.substr(pos, end - pos);
            std::vector<std::string> parts;
            std::stringstream ss(section);
            std::string part;
            int count = 0;
            while (count < 4 && std::getline(ss, part, ':')) {
                parts.push_back(trim(part));
                count++;
            }
            if (parts.size() >= 4) {
                DifficultyInfo info;
                info.stepsType = parts[0];
                info.difficulty = parts[2];
                try {
                    info.meter = std::stoi(parts[3]);
                } catch (...) {
                    info.meter = 1;
                }
                info.keyCount = stepsTypeToKeyCount(info.stepsType);
                diffs.push_back(info);
            }
        }
    }
    return diffs;
}
