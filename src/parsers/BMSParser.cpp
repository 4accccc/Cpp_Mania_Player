#include "BMSParser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

bool BMSParser::isBMSFile(const std::string& filepath) {
    std::string ext = fs::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".bms" || ext == ".bme" || ext == ".bml" || ext == ".pms";
}

std::string BMSParser::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

int BMSParser::base36ToInt(const std::string& str) {
    if (str.empty() || str == "00") return 0;
    int result = 0;
    for (char c : str) {
        result *= 36;
        if (c >= '0' && c <= '9') {
            result += c - '0';
        } else if (c >= 'A' && c <= 'Z') {
            result += c - 'A' + 10;
        } else if (c >= 'a' && c <= 'z') {
            result += c - 'a' + 10;
        }
    }
    return result;
}

std::string BMSParser::intToBase36(int value) {
    if (value == 0) return "00";
    std::string result;
    while (value > 0) {
        int digit = value % 36;
        if (digit < 10) {
            result = char('0' + digit) + result;
        } else {
            result = char('A' + digit - 10) + result;
        }
        value /= 36;
    }
    while (result.length() < 2) result = "0" + result;
    return result;
}

int BMSParser::channelToLane(int channel, int keyCount) {
    // 8-key (IIDX): 16,11,12,13,14,15,18,19 -> 0,1,2,3,4,5,6,7 (scratch + 7 keys)
    // 7-key mapping: 11,12,13,14,15,18,19 -> 0,1,2,3,4,5,6
    // 5-key mapping: 11,12,13,14,15 -> 0,1,2,3,4
    // Long note channels: 51-59 correspond to 11-19, 56 is scratch LN

    int baseChannel = channel;
    if (channel >= 51 && channel <= 59) {
        baseChannel = channel - 40;  // 51->11, 52->12, etc.
    }
    if (channel == 56) {
        baseChannel = 16;  // Scratch long note
    }

    if (keyCount == 8) {
        // IIDX 7+1 style: scratch on left
        switch (baseChannel) {
            case 16: return 0;  // Scratch
            case 11: return 1;
            case 12: return 2;
            case 13: return 3;
            case 14: return 4;
            case 15: return 5;
            case 18: return 6;
            case 19: return 7;
            default: return -1;
        }
    } else if (keyCount == 7) {
        switch (baseChannel) {
            case 11: return 0;
            case 12: return 1;
            case 13: return 2;
            case 14: return 3;
            case 15: return 4;
            case 18: return 5;
            case 19: return 6;
            default: return -1;
        }
    } else if (keyCount == 5) {
        switch (baseChannel) {
            case 11: return 0;
            case 12: return 1;
            case 13: return 2;
            case 14: return 3;
            case 15: return 4;
            default: return -1;
        }
    } else if (keyCount == 9) {
        // PMS 9-key: 11-15, 22-25
        switch (baseChannel) {
            case 11: return 0;
            case 12: return 1;
            case 13: return 2;
            case 14: return 3;
            case 15: return 4;
            case 22: return 5;
            case 23: return 6;
            case 24: return 7;
            case 25: return 8;
            default: return -1;
        }
    }
    return -1;
}

int BMSParser::detectKeyCount(const std::vector<BMSChannel>& channels) {
    bool hasScratch = false;   // Channel 16 or 56
    bool has18or19 = false;
    bool has22to25 = false;

    for (const auto& ch : channels) {
        int baseChannel = ch.channel;
        if (baseChannel >= 51 && baseChannel <= 59) {
            baseChannel -= 40;
        }
        if (baseChannel == 56) {
            baseChannel = 16;  // Long note scratch
        }
        if (baseChannel == 16) {
            hasScratch = true;
        }
        if (baseChannel == 18 || baseChannel == 19) {
            has18or19 = true;
        }
        if (baseChannel >= 22 && baseChannel <= 25) {
            has22to25 = true;
        }
    }

    if (has22to25) return 9;  // PMS
    if (hasScratch) return 8; // 7+1 (IIDX style with scratch)
    if (has18or19) return 7;
    return 5;
}

bool BMSParser::parse(const std::string& filepath, BeatmapInfo& info) {
    BMSData data;
    if (!parseFull(filepath, data)) {
        return false;
    }
    info = data.beatmap;
    return true;
}

bool BMSParser::parseFull(const std::string& filepath, BMSData& data) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open BMS file: " << filepath << std::endl;
        return false;
    }

    data.directory = fs::path(filepath).parent_path().string();
    BeatmapInfo& info = data.beatmap;

    // Default values
    info.mode = 3;  // mania
    info.od = 8.0f;
    info.hp = 5.0f;
    info.sliderMultiplier = 1.4f;

    double baseBpm = 130.0;
    std::string lnobj;  // LNOBJ marker

    // BPM change definitions
    std::unordered_map<int, double> bpmDefs;
    // STOP definitions
    std::unordered_map<int, int> stopDefs;
    // Measure length changes
    std::unordered_map<int, double> measureLengths;

    std::vector<BMSChannel> channels;
    std::string line;

    // First pass: parse header and channel data
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] != '#') continue;

        // Header fields
        if (line.rfind("#TITLE ", 0) == 0 || line.rfind("#TITLE\t", 0) == 0) {
            info.title = trim(line.substr(7));
        } else if (line.rfind("#ARTIST ", 0) == 0 || line.rfind("#ARTIST\t", 0) == 0) {
            info.artist = trim(line.substr(8));
        } else if (line.rfind("#BPM ", 0) == 0 && line.find(':') == std::string::npos) {
            baseBpm = std::stod(trim(line.substr(5)));
        } else if (line.rfind("#PLAYLEVEL ", 0) == 0) {
            info.version = "Lv." + trim(line.substr(11));
        } else if (line.rfind("#LNOBJ ", 0) == 0) {
            lnobj = trim(line.substr(7));
        }
        // WAV definition: #WAVxx filename
        else if (line.rfind("#WAV", 0) == 0 && line.length() > 6) {
            std::string idStr = line.substr(4, 2);
            std::string filename = trim(line.substr(7));
            int id = base36ToInt(idStr);
            data.wavDefs[id] = filename;
        }
        // BMP definition: #BMPxx filename
        else if (line.rfind("#BMP", 0) == 0 && line.length() > 6) {
            std::string idStr = line.substr(4, 2);
            std::string filename = trim(line.substr(7));
            int id = base36ToInt(idStr);
            data.bmpDefs[id] = filename;
        }
        // BPM definition: #BPMxx value
        else if (line.rfind("#BPM", 0) == 0 && line.length() > 6 && line[6] == ' ') {
            std::string idStr = line.substr(4, 2);
            double bpm = std::stod(trim(line.substr(7)));
            int id = base36ToInt(idStr);
            bpmDefs[id] = bpm;
        }
        // STOP definition: #STOPxx value
        else if (line.rfind("#STOP", 0) == 0 && line.length() > 7) {
            std::string idStr = line.substr(5, 2);
            int stopValue = std::stoi(trim(line.substr(8)));
            int id = base36ToInt(idStr);
            stopDefs[id] = stopValue;
        }
        // Main data field: #XXXYY:ZZZZZZ...
        else if (line.length() > 6 && line[6] == ':') {
            int measure = std::stoi(line.substr(1, 3));
            int channel = std::stoi(line.substr(4, 2));
            std::string dataStr = line.substr(7);

            // Measure length change (channel 02)
            if (channel == 2) {
                measureLengths[measure] = std::stod(dataStr);
                continue;
            }

            // Parse data pairs
            BMSChannel ch;
            ch.measure = measure;
            ch.channel = channel;
            for (size_t i = 0; i + 1 < dataStr.length(); i += 2) {
                ch.data.push_back(dataStr.substr(i, 2));
            }
            if (!ch.data.empty()) {
                channels.push_back(ch);
            }
        }
    }
    file.close();

    // Detect key count
    int keyCount = detectKeyCount(channels);
    info.keyCount = keyCount;

    // Find max measure number
    int maxMeasure = 0;
    for (const auto& ch : channels) {
        maxMeasure = std::max(maxMeasure, ch.measure);
    }

    // Calculate start time for each measure
    std::vector<double> measureStartTimes(maxMeasure + 2, 0.0);
    double currentTime = 0.0;
    double currentBpm = baseBpm;

    // Add initial timing point
    TimingPoint tp;
    tp.time = 0;
    tp.beatLength = 60000.0 / baseBpm;
    tp.uninherited = true;
    tp.effectiveBeatLength = tp.beatLength;
    info.timingPoints.push_back(tp);

    // Collect BPM change and STOP events for precise time calculation
    struct BpmEvent {
        int measure;
        int position;  // Position within measure (0-based index)
        int total;     // Total data count in measure
        double bpm;
        bool isStop;
        int stopValue;
    };
    std::vector<BpmEvent> bpmEvents;

    // Collect BPM change events
    for (const auto& ch : channels) {
        if (ch.channel == 3 || ch.channel == 8) {
            // Channel 03: hex BPM, Channel 08: extended BPM
            for (size_t i = 0; i < ch.data.size(); i++) {
                if (ch.data[i] == "00") continue;
                BpmEvent evt;
                evt.measure = ch.measure;
                evt.position = static_cast<int>(i);
                evt.total = static_cast<int>(ch.data.size());
                evt.isStop = false;
                if (ch.channel == 3) {
                    // Hex direct value
                    evt.bpm = static_cast<double>(std::stoi(ch.data[i], nullptr, 16));
                } else {
                    // Extended BPM reference
                    int id = base36ToInt(ch.data[i]);
                    if (bpmDefs.count(id)) {
                        evt.bpm = bpmDefs[id];
                    } else {
                        continue;
                    }
                }
                bpmEvents.push_back(evt);
            }
        }
    }

    // Collect STOP events
    for (const auto& ch : channels) {
        if (ch.channel == 9) {
            for (size_t i = 0; i < ch.data.size(); i++) {
                if (ch.data[i] == "00") continue;
                int id = base36ToInt(ch.data[i]);
                if (!stopDefs.count(id)) continue;
                BpmEvent evt;
                evt.measure = ch.measure;
                evt.position = static_cast<int>(i);
                evt.total = static_cast<int>(ch.data.size());
                evt.isStop = true;
                evt.stopValue = stopDefs[id];
                evt.bpm = 0;
                bpmEvents.push_back(evt);
            }
        }
    }

    // Sort BPM events by time
    std::sort(bpmEvents.begin(), bpmEvents.end(), [](const BpmEvent& a, const BpmEvent& b) {
        double posA = a.measure + static_cast<double>(a.position) / a.total;
        double posB = b.measure + static_cast<double>(b.position) / b.total;
        return posA < posB;
    });

    // Time calculation helper function
    auto calculateTime = [&](int measure, int position, int total) -> double {
        double measureLength = measureLengths.count(measure) ? measureLengths[measure] : 1.0;
        double posInMeasure = static_cast<double>(position) / total;
        double targetPos = measure + posInMeasure;

        double time = 0.0;
        double bpm = baseBpm;
        double lastPos = 0.0;

        for (const auto& evt : bpmEvents) {
            double evtPos = evt.measure + static_cast<double>(evt.position) / evt.total;
            if (evtPos >= targetPos) break;

            // Calculate time from lastPos to evtPos
            int startMeasure = static_cast<int>(lastPos);
            int endMeasure = static_cast<int>(evtPos);
            double fracStart = lastPos - startMeasure;
            double fracEnd = evtPos - endMeasure;

            if (startMeasure == endMeasure) {
                double ml = measureLengths.count(startMeasure) ? measureLengths[startMeasure] : 1.0;
                time += (fracEnd - fracStart) * ml * 4.0 * 60000.0 / bpm;
            } else {
                // Cross measure
                double ml = measureLengths.count(startMeasure) ? measureLengths[startMeasure] : 1.0;
                time += (1.0 - fracStart) * ml * 4.0 * 60000.0 / bpm;
                for (int m = startMeasure + 1; m < endMeasure; m++) {
                    ml = measureLengths.count(m) ? measureLengths[m] : 1.0;
                    time += ml * 4.0 * 60000.0 / bpm;
                }
                ml = measureLengths.count(endMeasure) ? measureLengths[endMeasure] : 1.0;
                time += fracEnd * ml * 4.0 * 60000.0 / bpm;
            }

            // Handle STOP
            if (evt.isStop) {
                time += evt.stopValue * 60000.0 / (bpm * 48.0);
            } else {
                bpm = evt.bpm;
            }
            lastPos = evtPos;
        }

        // Calculate remaining time from lastPos to targetPos
        int startMeasure = static_cast<int>(lastPos);
        int endMeasure = static_cast<int>(targetPos);
        double fracStart = lastPos - startMeasure;
        double fracEnd = targetPos - endMeasure;

        if (startMeasure == endMeasure) {
            double ml = measureLengths.count(startMeasure) ? measureLengths[startMeasure] : 1.0;
            time += (fracEnd - fracStart) * ml * 4.0 * 60000.0 / bpm;
        } else {
            double ml = measureLengths.count(startMeasure) ? measureLengths[startMeasure] : 1.0;
            time += (1.0 - fracStart) * ml * 4.0 * 60000.0 / bpm;
            for (int m = startMeasure + 1; m < endMeasure; m++) {
                ml = measureLengths.count(m) ? measureLengths[m] : 1.0;
                time += ml * 4.0 * 60000.0 / bpm;
            }
            ml = measureLengths.count(endMeasure) ? measureLengths[endMeasure] : 1.0;
            time += fracEnd * ml * 4.0 * 60000.0 / bpm;
        }
        return time;
    };

    // Track LNOBJ long note start notes
    std::unordered_map<int, std::pair<int64_t, int>> lnStarts;  // lane -> (startTime, wavId)

    // Track channel 51-59 long note starts
    std::unordered_map<int, int64_t> lnChannelStarts;  // lane -> startTime

    // Process note channels
    for (const auto& ch : channels) {
        int channel = ch.channel;
        bool isNoteChannel = (channel >= 11 && channel <= 19) ||
                             (channel >= 21 && channel <= 29);
        bool isLnChannel = (channel >= 51 && channel <= 59);
        bool isBgmChannel = (channel == 1);
        bool isBgaChannel = (channel == 4 || channel == 6 || channel == 7);

        if (!isNoteChannel && !isLnChannel && !isBgmChannel && !isBgaChannel) continue;

        for (size_t i = 0; i < ch.data.size(); i++) {
            if (ch.data[i] == "00") continue;

            int64_t time = static_cast<int64_t>(calculateTime(ch.measure, static_cast<int>(i), static_cast<int>(ch.data.size())));
            int wavId = base36ToInt(ch.data[i]);

            // BGM channel
            if (isBgmChannel) {
                StoryboardSample sample;
                sample.time = time;
                sample.volume = 100;
                sample.sampleHandle = wavId;
                if (data.wavDefs.count(wavId)) {
                    sample.filename = data.wavDefs[wavId];
                }
                info.storyboardSamples.push_back(sample);
                continue;
            }

            // BGA channel
            if (isBgaChannel) {
                BMSBgaEvent evt;
                evt.time = time;
                evt.bmpId = wavId;
                if (channel == 4) evt.layer = 0;       // BGA base
                else if (channel == 7) evt.layer = 1;  // Layer
                else evt.layer = 2;                     // Poor (channel 6)
                if (data.bmpDefs.count(wavId)) {
                    evt.filename = data.bmpDefs[wavId];
                }
                data.bgaEvents.push_back(evt);
                continue;
            }

            // Note channel
            int lane = channelToLane(channel, keyCount);
            if (lane < 0) continue;

            // Check if this is LNOBJ end marker
            if (!lnobj.empty() && ch.data[i] == lnobj) {
                if (lnStarts.count(lane)) {
                    auto& start = lnStarts[lane];
                    Note note(lane, start.first, true, time);
                    note.customIndex = start.second;
                    info.notes.push_back(note);
                    lnStarts.erase(lane);
                }
                continue;
            }

            // Long note channel (51-59)
            if (isLnChannel) {
                if (lnChannelStarts.count(lane)) {
                    // End long note
                    Note note(lane, lnChannelStarts[lane], true, time);
                    note.customIndex = wavId;
                    info.notes.push_back(note);
                    lnChannelStarts.erase(lane);
                } else {
                    // Start long note
                    lnChannelStarts[lane] = time;
                }
                continue;
            }

            // Normal note - if LNOBJ exists, record as potential long note start
            if (!lnobj.empty()) {
                lnStarts[lane] = {time, wavId};
            }

            // Create normal note
            Note note(lane, time);
            note.customIndex = wavId;
            info.notes.push_back(note);
        }
    }

    // Sort notes by time
    std::sort(info.notes.begin(), info.notes.end(), [](const Note& a, const Note& b) {
        return a.time < b.time;
    });

    // Sort BGA events by time
    std::sort(data.bgaEvents.begin(), data.bgaEvents.end(), [](const BMSBgaEvent& a, const BMSBgaEvent& b) {
        return a.time < b.time;
    });

    // Sort storyboard samples by time
    std::sort(info.storyboardSamples.begin(), info.storyboardSamples.end(),
        [](const StoryboardSample& a, const StoryboardSample& b) {
            return a.time < b.time;
        });

    info.totalObjectCount = static_cast<int>(info.notes.size());
    info.endTimeObjectCount = 0;
    for (const auto& note : info.notes) {
        if (note.isHold) info.endTimeObjectCount++;
    }

    std::cout << "Loaded BMS: " << info.title << " by " << info.artist << std::endl;
    std::cout << "  " << info.keyCount << "K, " << info.notes.size() << " notes, "
              << data.bgaEvents.size() << " BGA events" << std::endl;

    return true;
}
