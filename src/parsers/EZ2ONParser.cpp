#include "EZ2ONParser.h"
#include "OsuParser.h"
#include <fstream>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <sstream>
#include <filesystem>
#include <set>
#include <iostream>

namespace fs = std::filesystem;

static const int TICKS_PER_BEAT = 48;
static const int TAP_DURATION_THRESHOLD = 6;

// ---------------------------------------------------------------------------
// File extension + EZFF magic check
// ---------------------------------------------------------------------------
bool EZ2ONParser::isEZ2ONFile(const std::string& filepath) {
    size_t dot = filepath.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = filepath.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext != ".ezi") return false;

    // Verify EZFF magic to distinguish from encrypted EZ2AC .ezi files
    std::ifstream f(filepath, std::ios::binary);
    if (!f) return false;
    char magic[4];
    if (!f.read(magic, 4)) return false;
    return memcmp(magic, "EZFF", 4) == 0;
}

// ---------------------------------------------------------------------------
// Parse plaintext .ez keysound index
// Format: "<id> <flag> <filename>" per line
// flag=1 -> BGM, flag=0 -> keysound
// ---------------------------------------------------------------------------
bool EZ2ONParser::parseKeysoundIndex(const std::string& ezPath,
                                     std::vector<std::string>& sampleMap,
                                     std::string& bgmFilename) {
    std::ifstream file(ezPath);
    if (!file) return false;

    sampleMap.clear();
    bgmFilename.clear();

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty()) continue;

        std::istringstream ls(line);
        int id = 0, flag = 0;
        std::string filename;
        if (!(ls >> id >> flag >> filename)) continue;

        if (id >= (int)sampleMap.size())
            sampleMap.resize(id + 1);
        sampleMap[id] = filename;

        if (flag == 1 && bgmFilename.empty())
            bgmFilename = filename;
    }

    return !sampleMap.empty();
}

// ---------------------------------------------------------------------------
// Tick -> milliseconds conversion with BPM timeline
// ---------------------------------------------------------------------------
double EZ2ONParser::tickToMs(uint32_t tick, const std::vector<BPMEvent>& bpmTimeline,
                              int ticksPerBeat) {
    if (bpmTimeline.empty()) return 0.0;

    const BPMEvent* active = &bpmTimeline[0];
    for (size_t i = 1; i < bpmTimeline.size(); i++) {
        if (bpmTimeline[i].tick <= tick)
            active = &bpmTimeline[i];
        else
            break;
    }

    double msPerTick = 60000.0 / (active->bpm * ticksPerBeat);
    return active->timeMs + (tick - active->tick) * msPerTick;
}

// ---------------------------------------------------------------------------
// Auto-detect mode by scanning which channels have type=1 note events
// EZ2ON uses consecutive channels starting from ch3:
//   4K = ch3-6, 5K = ch3-7, 6K = ch3-8, 8K = ch3-10
// ---------------------------------------------------------------------------
std::pair<int, std::vector<int>> EZ2ONParser::detectMode(const std::vector<uint8_t>& data) {
    if (data.size() < 150) return {0, {}};

    uint16_t channelCount;
    memcpy(&channelCount, &data[140], 2);
    if (channelCount > 96) channelCount = 96;

    // Track which channels (0-21) contain type=1 note events
    std::set<int> usedChannels;
    size_t offset = 150;
    for (int ch = 0; ch < channelCount; ch++) {
        if (offset + 78 > data.size()) break;
        uint32_t dataSize;
        memcpy(&dataSize, &data[offset + 74], 4);
        offset += 78;
        if (offset + dataSize > data.size()) break;

        // Only check channels in playable range (3-21)
        if (ch >= 3 && ch < 22) {
            int noteCount = (int)(dataSize / 13);
            for (int n = 0; n < noteCount; n++) {
                size_t noff = offset + n * 13;
                if (noff + 13 > data.size()) break;
                uint8_t type = data[noff + 4];
                if (type == 1) {
                    usedChannels.insert(ch);
                    break; // one note is enough to know this channel is used
                }
            }
        }
        offset += dataSize;
    }

    if (usedChannels.empty()) return {0, {}};

    // Build playable channel list from consecutive channels starting at ch3
    int maxCh = *usedChannels.rbegin();
    std::vector<int> playableChannels;
    for (int ch = 3; ch <= maxCh; ch++) {
        playableChannels.push_back(ch);
    }

    int keyCount = (int)playableChannels.size();
    return {keyCount, playableChannels};
}

// ---------------------------------------------------------------------------
// Parse EZFF binary chart data (no decryption)
// Same structure as EZ2AC: 150-byte header, 78-byte channel headers,
// 13-byte note structures
// ---------------------------------------------------------------------------
bool EZ2ONParser::parseEZFF(const std::vector<uint8_t>& data,
                             const std::string& filepath,
                             int keyCount,
                             const std::vector<int>& playableChannels,
                             BeatmapInfo& info) {
    if (data.size() < 150) return false;
    if (memcmp(data.data(), "EZFF", 4) != 0) return false;

    // Header: offset 6-69 name1, offset 70-133 name2
    std::string name1(data.begin() + 6, data.begin() + 70);
    name1 = name1.c_str(); // trim at null
    std::string name2(data.begin() + 70, data.begin() + 134);
    name2 = name2.c_str();

    int ticksPerBeat = TICKS_PER_BEAT;

    // offset 140: channel count
    uint16_t channelCount;
    memcpy(&channelCount, &data[140], 2);
    if (channelCount > 96) channelCount = 96;

    // Build channel -> lane lookup (-1 = not playable)
    int channelToLane[96];
    memset(channelToLane, -1, sizeof(channelToLane));
    for (int i = 0; i < (int)playableChannels.size(); i++) {
        int ch = playableChannels[i];
        if (ch < 96) channelToLane[ch] = i;
    }

    // Load keysound map from .ez file in same directory
    std::vector<std::string> sampleMap;
    std::string bgmFilename;
    {
        fs::path chartPath(filepath);
        fs::path dir = chartPath.parent_path();
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".ez") {
                parseKeysoundIndex(entry.path().string(), sampleMap, bgmFilename);
                break; // use first .ez found
            }
        }
    }

    // First pass: collect BPM events (type 3 and 5)
    std::vector<BPMEvent> bpmTimeline;
    {
        size_t offset = 150;
        for (int ch = 0; ch < channelCount; ch++) {
            if (offset + 78 > data.size()) break;
            uint32_t dataSize;
            memcpy(&dataSize, &data[offset + 74], 4);
            offset += 78;
            if (offset + dataSize > data.size()) break;

            int noteCount = (int)(dataSize / 13);
            for (int n = 0; n < noteCount; n++) {
                size_t noff = offset + n * 13;
                if (noff + 13 > data.size()) break;
                uint32_t position;
                memcpy(&position, &data[noff], 4);
                uint8_t type = data[noff + 4];

                if (type == 3 || type == 5) {
                    float bpm;
                    memcpy(&bpm, &data[noff + 5], 4);
                    if (bpm >= 1.0f && bpm < 10000.0f) {
                        bpmTimeline.push_back({position, bpm, 0.0});
                    }
                }
            }
            offset += dataSize;
        }
    }

    // Sort BPM events, remove duplicates at same tick (keep last)
    std::sort(bpmTimeline.begin(), bpmTimeline.end(),
              [](const BPMEvent& a, const BPMEvent& b) { return a.tick < b.tick; });
    for (int i = (int)bpmTimeline.size() - 2; i >= 0; i--) {
        if (bpmTimeline[i].tick == bpmTimeline[i + 1].tick)
            bpmTimeline.erase(bpmTimeline.begin() + i);
    }

    // Fallback: if no BPM events found, use header BPM (offset 136)
    if (bpmTimeline.empty()) {
        float headerBpm;
        memcpy(&headerBpm, &data[136], 4);
        if (headerBpm >= 1.0f && headerBpm < 10000.0f) {
            bpmTimeline.push_back({0, headerBpm, 0.0});
        } else {
            bpmTimeline.push_back({0, 120.0f, 0.0}); // last resort default
        }
    }

    // Compute absolute times for BPM events
    if (!bpmTimeline.empty()) {
        bpmTimeline[0].timeMs = 0.0;
        for (size_t i = 1; i < bpmTimeline.size(); i++) {
            double msPerTick = 60000.0 / (bpmTimeline[i - 1].bpm * ticksPerBeat);
            uint32_t deltaTick = bpmTimeline[i].tick - bpmTimeline[i - 1].tick;
            bpmTimeline[i].timeMs = bpmTimeline[i - 1].timeMs + deltaTick * msPerTick;
        }
    }

    // Build timing points
    for (auto& ev : bpmTimeline) {
        TimingPoint tp;
        tp.time = ev.timeMs;
        tp.beatLength = 60000.0 / ev.bpm;
        tp.uninherited = true;
        tp.effectiveBeatLength = tp.beatLength;
        tp.volume = 100;
        info.timingPoints.push_back(tp);
    }

    // Second pass: parse notes
    {
        size_t offset = 150;
        for (int ch = 0; ch < channelCount; ch++) {
            if (offset + 78 > data.size()) break;
            uint32_t dataSize;
            memcpy(&dataSize, &data[offset + 74], 4);
            offset += 78;
            if (offset + dataSize > data.size()) break;

            int lane = (ch < 96) ? channelToLane[ch] : -1;
            bool isPlayable = (lane >= 0);
            bool isBGM = (ch >= 22 && !isPlayable);

            int noteCount = (int)(dataSize / 13);
            for (int n = 0; n < noteCount; n++) {
                size_t noff = offset + n * 13;
                if (noff + 13 > data.size()) break;

                uint32_t position;
                memcpy(&position, &data[noff], 4);
                uint8_t type = data[noff + 4];
                if (type != 1) continue;

                uint16_t sampleId;
                memcpy(&sampleId, &data[noff + 5], 2);
                uint8_t volume = data[noff + 7];
                uint16_t duration;
                memcpy(&duration, &data[noff + 10], 2);

                double timeMs = tickToMs(position, bpmTimeline, ticksPerBeat);
                int64_t noteTime = (int64_t)std::round(timeMs);

                std::string sampleFilename;
                if (sampleId < sampleMap.size() && !sampleMap[sampleId].empty())
                    sampleFilename = sampleMap[sampleId];

                if (isPlayable) {
                    bool isHold = (duration > TAP_DURATION_THRESHOLD);
                    int64_t endTime = 0;
                    if (isHold) {
                        double endMs = tickToMs(position + duration, bpmTimeline, ticksPerBeat);
                        endTime = (int64_t)std::round(endMs);
                    }

                    Note note(lane, noteTime, isHold, endTime);
                    note.customIndex = sampleId;
                    note.filename = sampleFilename;
                    note.volume = (volume > 0) ? (int)(volume * 100 / 127) : 100;
                    info.notes.push_back(note);
                } else if (isBGM) {
                    StoryboardSample ss;
                    ss.time = noteTime;
                    ss.filename = sampleFilename;
                    ss.volume = (volume > 0) ? (int)(volume * 100 / 127) : 100;
                    info.storyboardSamples.push_back(ss);
                }
            }
            offset += dataSize;
        }
    }

    // Sort notes and samples
    std::sort(info.notes.begin(), info.notes.end(),
              [](const Note& a, const Note& b) { return a.time < b.time; });
    std::sort(info.storyboardSamples.begin(), info.storyboardSamples.end(),
              [](const StoryboardSample& a, const StoryboardSample& b) {
                  return a.time < b.time;
              });

    // Metadata
    info.title = name1.empty() ? name2 : name1;
    info.mode = 3;
    info.keyCount = keyCount;
    info.od = 8.0f;
    info.hp = 8.0f;
    info.cs = 0;
    info.ar = 0;
    info.sliderMultiplier = 0;
    // EZ2ON is a keysound-based format: BGM is triggered as a storyboard
    // sample at the correct chart position (not necessarily time 0).
    // Do NOT set audioFilename — the game will run in keysound-only mode
    // and the BGM sample will play at its scheduled time.
    info.audioFilename.clear();
    info.totalObjectCount = (int)info.notes.size();
    info.endTimeObjectCount = 0;
    for (auto& n : info.notes) {
        if (n.isHold) info.endTimeObjectCount++;
    }

    // Version string: parse name1 for difficulty (format: "<keyCount>-<diff>")
    // e.g. "4-shd" -> "4K SHD", "6-nm" -> "6K NM"
    info.version = std::to_string(keyCount) + "K";
    if (name1.length() >= 3) {
        size_t dash = name1.find('-');
        if (dash != std::string::npos && dash > 0) {
            std::string diffStr = name1.substr(dash + 1);
            std::transform(diffStr.begin(), diffStr.end(), diffStr.begin(), ::toupper);
            if (diffStr == "EZ" || diffStr == "NM" || diffStr == "HD" || diffStr == "SHD")
                info.version += " " + diffStr;
        }
    }

    return !info.notes.empty();
}

// ---------------------------------------------------------------------------
// Main entry point: parse .ezi file (EZFF binary chart, no encryption)
// ---------------------------------------------------------------------------
bool EZ2ONParser::parse(const std::string& filepath, BeatmapInfo& info) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return false;

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    file.close();

    if (data.size() < 4 || memcmp(data.data(), "EZFF", 4) != 0)
        return false;

    // Auto-detect mode from channel structure
    auto [keyCount, playableChannels] = detectMode(data);
    if (keyCount == 0 || playableChannels.empty()) {
        std::cerr << "EZ2ON: could not detect mode from " << filepath << std::endl;
        return false;
    }

    if (!parseEZFF(data, filepath, keyCount, playableChannels, info))
        return false;

    info.beatmapHash = OsuParser::calculateMD5(filepath);

    fs::path p(filepath);
    std::string dirName = p.parent_path().filename().string();
    if (info.title.empty() || info.title == dirName)
        info.title = dirName;
    if (info.artist.empty()) info.artist = "EZ2ON";
    if (info.creator.empty()) info.creator = "EZ2ON";

    std::cout << "Loaded EZ2ON chart: " << keyCount << "K, "
              << info.notes.size() << " notes" << std::endl;

    return true;
}
