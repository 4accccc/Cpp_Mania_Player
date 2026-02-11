#include "2dxParser.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <map>

// Difficulty to file offset mapping
// .1 file has 11 directory entries, each 8 bytes (offset + size)
int IIDXParser::getDifficultyOffset(int difficulty) {
    // Simple linear mapping: index * 8
    // Index 0-1: SP Hyper/Normal (order may vary, need swap based on note count)
    // Index 2: SP Another
    // Index 3: SP Beginner
    // Index 4: SP Leggendaria
    // Index 5: DP Beginner
    // Index 6-7: DP Hyper/Normal (order may vary)
    // Index 8: DP Another
    // Index 9: DP Leggendaria
    return difficulty * 8;
}

const char* IIDXParser::getDifficultyName(int difficulty) {
    switch (difficulty) {
        case 0: return "SP HYPER";
        case 1: return "SP NORMAL";
        case 2: return "SP ANOTHER";
        case 3: return "SP BEGINNER";
        case 4: return "SP LEGGENDARIA";
        // case 5: NULL (unused)
        case 6: return "DP HYPER";
        case 7: return "DP NORMAL";
        case 8: return "DP ANOTHER";
        case 9: return "DP BEGINNER";
        case 10: return "DP LEGGENDARIA";
        default: return "Unknown";
    }
}

bool IIDXParser::hasDifficulty(const std::string& path, int difficulty) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    int offset = getDifficultyOffset(difficulty);

    // Read offset table entry
    file.seekg(offset, std::ios::beg);
    uint32_t dataOffset = 0, dataSize = 0;
    file.read(reinterpret_cast<char*>(&dataOffset), 4);
    file.read(reinterpret_cast<char*>(&dataSize), 4);

    return dataOffset > 0 && dataSize > 0;
}

std::vector<int> IIDXParser::getAvailableDifficulties(const std::string& path) {
    std::vector<int> result;
    std::ifstream file(path, std::ios::binary);
    if (!file) return result;

    for (int i = 0; i <= 9; i++) {
        int offset = getDifficultyOffset(i);
        file.seekg(offset, std::ios::beg);

        uint32_t dataOffset = 0, dataSize = 0;
        file.read(reinterpret_cast<char*>(&dataOffset), 4);
        file.read(reinterpret_cast<char*>(&dataSize), 4);

        if (dataOffset > 0 && dataSize > 0) {
            result.push_back(i);
        }
    }
    return result;
}

bool IIDXParser::parse(const std::string& path, BeatmapInfo& info, int difficulty) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "IIDXParser: Failed to open file: " << path << std::endl;
        return false;
    }

    // Get file size
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // Read offset table entry for this difficulty
    int tableOffset = getDifficultyOffset(difficulty);
    file.seekg(tableOffset, std::ios::beg);

    uint32_t dataOffset = 0, dataSize = 0;
    file.read(reinterpret_cast<char*>(&dataOffset), 4);
    file.read(reinterpret_cast<char*>(&dataSize), 4);

    if (dataOffset == 0 || dataSize == 0) {
        std::cerr << "IIDXParser: Difficulty " << getDifficultyName(difficulty)
                  << " not found in file" << std::endl;
        return false;
    }

    if (dataOffset + dataSize > fileSize) {
        std::cerr << "IIDXParser: Invalid data offset/size" << std::endl;
        return false;
    }

    // Read chart data
    std::vector<uint8_t> chartData(dataSize);
    file.seekg(dataOffset, std::ios::beg);
    file.read(reinterpret_cast<char*>(chartData.data()), dataSize);

    // Set basic info
    info.mode = 3;  // mania mode
    // SP = 7 keys + 1 scratch = 8, DP = 14 keys + 2 scratches = 16
    info.keyCount = (difficulty >= 5) ? 16 : 8;
    info.version = getDifficultyName(difficulty);

    // Parse events
    return parseEvents(chartData.data(), chartData.size(), info);
}

bool IIDXParser::parseEvents(const uint8_t* data, size_t size, BeatmapInfo& info) {
    if (size < 8) return false;

    double currentBPM = 120.0;
    int numerator = 4, denominator = 4;

    // Keysound data: lane -> list of (time, sampleId)
    std::map<int, std::vector<std::pair<int64_t, int>>> laneSamples;

    // First pass: find end time and collect keysound events
    int64_t endTime = INT64_MAX;
    size_t offset = 0;
    while (offset + 8 <= size) {
        uint32_t tick = *reinterpret_cast<const uint32_t*>(data + offset);
        uint8_t type = data[offset + 4];
        uint8_t param = data[offset + 5];
        int16_t value = *reinterpret_cast<const int16_t*>(data + offset + 6);
        int64_t timeMs = static_cast<int64_t>(tick);

        if (type == EVENT_END) {
            endTime = timeMs;
        } else if (type == EVENT_SAMPLE_KEY) {
            // Keysound for key lanes: param 0-6 -> lanes 1-7, param 7 -> lane 0 (scratch)
            int lane = (param == 7) ? 0 : (param + 1);
            laneSamples[lane].push_back({timeMs, value});
        } else if (type == EVENT_SAMPLE_SCRATCH) {
            // Keysound for DP second side
            int lane = param + 8;
            laneSamples[lane].push_back({timeMs, value});
        }
        offset += 8;
    }

    // Sort keysound events by time for each lane
    for (auto& [lane, samples] : laneSamples) {
        std::sort(samples.begin(), samples.end());
    }

    // Helper: find sample ID for a note at given lane and time
    auto findSampleId = [&laneSamples](int lane, int64_t time) -> int {
        auto it = laneSamples.find(lane);
        if (it == laneSamples.end() || it->second.empty()) {
            return -1;
        }
        const auto& samples = it->second;
        // Binary search: find largest time <= note time
        int left = 0, right = static_cast<int>(samples.size()) - 1;
        while (left <= right) {
            int mid = (left + right) / 2;
            if (samples[mid].first <= time) {
                left = mid + 1;
            } else {
                right = mid - 1;
            }
        }
        if (right >= 0) {
            return samples[right].second;
        }
        return -1;
    };

    // Process events (8 bytes each)
    offset = 0;
    while (offset + 8 <= size) {
        // Read event
        uint32_t tick = *reinterpret_cast<const uint32_t*>(data + offset);
        uint8_t type = data[offset + 4];
        uint8_t param = data[offset + 5];
        int16_t value = *reinterpret_cast<const int16_t*>(data + offset + 6);

        // Time in milliseconds (1 tick = 1 ms for modern IIDX)
        int64_t timeMs = static_cast<int64_t>(tick);

        // Skip events after end marker
        if (timeMs > endTime && type != EVENT_END) {
            offset += 8;
            continue;
        }

        switch (type) {
            case EVENT_NOTE_KEY: {
                // Key note: param 0-6 = keys 1-7, param 7 = scratch
                // IIDX layout: scratch on left, then 7 keys
                // Lane mapping: scratch -> 0, keys -> 1-7
                int lane;
                if (param == 7) {
                    lane = 0;  // Scratch on leftmost
                } else {
                    lane = param + 1;  // Keys 0-6 -> lanes 1-7
                }

                if (lane < info.keyCount) {
                    bool isHold = (value > 0);
                    int64_t noteEndTime = isHold ? (timeMs + static_cast<int64_t>(value)) : 0;
                    Note note(lane, timeMs, isHold, noteEndTime);
                    note.customIndex = findSampleId(lane, timeMs);
                    info.notes.push_back(note);
                }
                break;
            }

            case EVENT_NOTE_SCRATCH: {
                // Scratch note for DP (second turntable)
                // For SP, this shouldn't appear much
                int lane = param + 8;  // DP second side
                if (lane < info.keyCount) {
                    bool isHold = (value > 0);
                    int64_t noteEndTime = isHold ? (timeMs + static_cast<int64_t>(value)) : 0;
                    Note note(lane, timeMs, isHold, noteEndTime);
                    note.customIndex = findSampleId(lane, timeMs);
                    info.notes.push_back(note);
                }
                break;
            }

            case EVENT_SAMPLE_KEY:
            case EVENT_SAMPLE_SCRATCH: {
                // Keysound events - ignored for now (will implement later)
                // value = sample ID
                break;
            }

            case EVENT_BPM: {
                // BPM change: BPM = value / param
                if (param > 0) {
                    currentBPM = static_cast<double>(value) / static_cast<double>(param);
                } else {
                    currentBPM = static_cast<double>(value);
                }
                if (currentBPM > 0) {
                    TimingPoint tp;
                    tp.time = static_cast<double>(timeMs);
                    tp.beatLength = 60000.0 / currentBPM;
                    tp.uninherited = true;
                    tp.effectiveBeatLength = tp.beatLength;
                    info.timingPoints.push_back(tp);
                }
                break;
            }

            case EVENT_TIMESIG: {
                // Time signature
                numerator = param;
                denominator = (value & 0xFF);
                if (denominator == 0) denominator = 4;
                break;
            }

            case EVENT_END: {
                // End marker - stop parsing
                goto done;
            }

            case EVENT_BGM: {
                // BGM sample trigger: value = sample ID, param = stereo flag
                StoryboardSample bgmSample;
                bgmSample.time = timeMs;
                bgmSample.layer = 0;
                bgmSample.volume = 100;
                bgmSample.customIndex = value;  // Sample ID
                info.storyboardSamples.push_back(bgmSample);
                break;
            }

            case EVENT_PARAM:
            case EVENT_MEASURE:
            case EVENT_HEADER:
            default:
                // Ignored
                break;
        }

        offset += 8;
    }

done:
    // Sort notes by time
    std::sort(info.notes.begin(), info.notes.end(),
        [](const Note& a, const Note& b) { return a.time < b.time; });

    // Add default timing point if none exists
    if (info.timingPoints.empty()) {
        TimingPoint tp;
        tp.time = 0;
        tp.beatLength = 500.0;  // 120 BPM default
        tp.uninherited = true;
        tp.effectiveBeatLength = tp.beatLength;
        info.timingPoints.push_back(tp);
    }

    // Count note types for debug
    int normalCount = 0, holdCount = 0;
    for (const auto& note : info.notes) {
        if (note.isHold) holdCount++;
        else normalCount++;
    }

    std::cout << "IIDXParser: Loaded " << info.notes.size() << " notes ("
              << normalCount << " normal, " << holdCount << " hold), "
              << info.timingPoints.size() << " timing points" << std::endl;

    return !info.notes.empty();
}
