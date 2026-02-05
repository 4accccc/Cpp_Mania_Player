#include "OjnParser.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cmath>

bool OjnParser::isOjnFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return false;

    // Check file extension
    size_t dotPos = filepath.rfind('.');
    if (dotPos != std::string::npos) {
        std::string ext = filepath.substr(dotPos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".ojn") return false;
    }

    // Check signature at offset 4
    file.seekg(4);
    char sig[4];
    file.read(sig, 4);

    // OJN signature is "ojn\0" at offset 4
    return (sig[0] == 'o' && sig[1] == 'j' && sig[2] == 'n');
}

std::string OjnParser::readString(const char* buffer, size_t maxLen) {
    size_t len = 0;
    while (len < maxLen && buffer[len] != '\0') {
        len++;
    }
    return std::string(buffer, len);
}

bool OjnParser::getHeader(const std::string& filepath, OjnHeader& header) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return false;

    // Read header (300 bytes)
    file.read(reinterpret_cast<char*>(&header.songId), 4);
    file.read(header.signature, 4);
    file.read(reinterpret_cast<char*>(&header.encodeVersion), 4);
    file.read(reinterpret_cast<char*>(&header.genre), 4);
    file.read(reinterpret_cast<char*>(&header.bpm), 4);

    // Level (4 x int16)
    for (int i = 0; i < 4; i++) {
        file.read(reinterpret_cast<char*>(&header.level[i]), 2);
    }

    // Event counts (3 x int32)
    for (int i = 0; i < 3; i++) {
        file.read(reinterpret_cast<char*>(&header.eventCount[i]), 4);
    }

    // Note counts (3 x int32)
    for (int i = 0; i < 3; i++) {
        file.read(reinterpret_cast<char*>(&header.noteCount[i]), 4);
    }

    // Measure counts (3 x int32)
    for (int i = 0; i < 3; i++) {
        file.read(reinterpret_cast<char*>(&header.measureCount[i]), 4);
    }

    // Package counts (3 x int32)
    for (int i = 0; i < 3; i++) {
        file.read(reinterpret_cast<char*>(&header.packageCount[i]), 4);
    }

    file.read(reinterpret_cast<char*>(&header.oldEncode), 2);
    file.read(reinterpret_cast<char*>(&header.oldSongId), 2);
    file.read(header.oldGenre, 20);
    file.read(reinterpret_cast<char*>(&header.bmpSize), 4);
    file.read(reinterpret_cast<char*>(&header.oldFileVersion), 4);
    file.read(header.title, 64);
    file.read(header.artist, 32);
    file.read(header.noter, 32);
    file.read(header.ojmFile, 32);
    file.read(reinterpret_cast<char*>(&header.coverSize), 4);

    // Time (3 x int32)
    for (int i = 0; i < 3; i++) {
        file.read(reinterpret_cast<char*>(&header.time[i]), 4);
    }

    // Note offsets (3 x int32)
    for (int i = 0; i < 3; i++) {
        file.read(reinterpret_cast<char*>(&header.noteOffset[i]), 4);
    }

    file.read(reinterpret_cast<char*>(&header.coverOffset), 4);

    return file.good();
}

std::vector<OjnDifficulty> OjnParser::getAvailableDifficulties(const std::string& filepath) {
    std::vector<OjnDifficulty> result;
    OjnHeader header;

    if (!getHeader(filepath, header)) {
        return result;
    }

    // Check which difficulties have notes
    if (header.noteCount[0] > 0) result.push_back(OjnDifficulty::Easy);
    if (header.noteCount[1] > 0) result.push_back(OjnDifficulty::Normal);
    if (header.noteCount[2] > 0) result.push_back(OjnDifficulty::Hard);

    return result;
}

int64_t OjnParser::measureToMs(int measure, int position, float bpm,
                                const std::vector<std::pair<int, float>>& bpmChanges) {
    // O2Jam uses 192 ticks per measure (48 ticks per beat, 4 beats per measure)
    const int TICKS_PER_MEASURE = 192;

    // Find effective BPM at this position
    float currentBpm = bpm;
    int64_t totalMs = 0;
    int currentMeasure = 0;
    int currentPosition = 0;

    for (const auto& change : bpmChanges) {
        int changeMeasure = change.first / TICKS_PER_MEASURE;
        int changePosition = change.first % TICKS_PER_MEASURE;

        if (changeMeasure > measure || (changeMeasure == measure && changePosition > position)) {
            break;
        }

        // Calculate time from current position to BPM change
        int ticksToChange = (changeMeasure - currentMeasure) * TICKS_PER_MEASURE +
                           (changePosition - currentPosition);
        double msPerTick = 60000.0 / (currentBpm * 48.0);  // 48 ticks per beat
        totalMs += static_cast<int64_t>(ticksToChange * msPerTick);

        currentBpm = change.second;
        currentMeasure = changeMeasure;
        currentPosition = changePosition;
    }

    // Calculate remaining time to target position
    int ticksRemaining = (measure - currentMeasure) * TICKS_PER_MEASURE +
                        (position - currentPosition);
    double msPerTick = 60000.0 / (currentBpm * 48.0);
    totalMs += static_cast<int64_t>(ticksRemaining * msPerTick);

    return totalMs;
}

bool OjnParser::parse(const std::string& filepath, BeatmapInfo& info,
                      OjnDifficulty difficulty) {
    OjnHeader header;
    if (!getHeader(filepath, header)) {
        return false;
    }

    int diffIdx = static_cast<int>(difficulty);

    // Fill metadata
    info.title = readString(header.title, 64);
    info.artist = readString(header.artist, 32);
    info.creator = readString(header.noter, 32);
    info.keyCount = 7;  // O2Jam is always 7 keys
    info.mode = 3;      // osu!mania mode

    // Set difficulty name
    const char* diffNames[] = {"Easy", "Normal", "Hard"};
    info.version = diffNames[diffIdx];

    // Set OD based on level
    info.od = static_cast<float>(header.level[diffIdx]) / 10.0f;
    if (info.od > 10.0f) info.od = 10.0f;

    info.notes.clear();
    info.timingPoints.clear();

    // Open file and seek to note data
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return false;

    file.seekg(header.noteOffset[diffIdx]);

    // BPM changes list
    std::vector<std::pair<int, float>> bpmChanges;
    float baseBpm = header.bpm;

    // Track hold note starts (per lane)
    int64_t holdStart[7] = {-1, -1, -1, -1, -1, -1, -1};
    int holdSampleId[7] = {0, 0, 0, 0, 0, 0, 0};

    // Read packages
    int packageCount = header.packageCount[diffIdx];
    for (int pkg = 0; pkg < packageCount; pkg++) {
        // Package header: measure(4) + channel(2) + eventCount(2)
        int32_t measure;
        int16_t channel;
        int16_t eventCount;

        file.read(reinterpret_cast<char*>(&measure), 4);
        file.read(reinterpret_cast<char*>(&channel), 2);
        file.read(reinterpret_cast<char*>(&eventCount), 2);

        if (!file.good()) break;

        // Read events for this package
        for (int evt = 0; evt < eventCount; evt++) {
            int16_t sampleId;
            int8_t pan;
            int8_t noteType;

            file.read(reinterpret_cast<char*>(&sampleId), 2);
            file.read(reinterpret_cast<char*>(&pan), 1);
            file.read(reinterpret_cast<char*>(&noteType), 1);

            // Calculate position in ticks
            int position = (evt * 192) / eventCount;
            int totalTicks = measure * 192 + position;

            // Channel 0: measure marker (skip)
            // Channel 1: BPM change
            if (channel == 1 && sampleId != 0) {
                float newBpm;
                memcpy(&newBpm, &sampleId, sizeof(float));
                if (newBpm > 0) {
                    bpmChanges.push_back({totalTicks, newBpm});
                }
                continue;
            }

            // Channel 2-8: note tracks (7 keys)
            if (channel >= 2 && channel <= 8 && sampleId != 0) {
                int lane = channel - 2;
                int64_t timeMs = measureToMs(measure, position, baseBpm, bpmChanges);

                // Determine actual sample ID based on noteType
                // noteType / 4: 0 = keysound, 1 = BGM sample
                int actualSampleId;
                if (noteType / 4 == 0) {
                    actualSampleId = sampleId + 1;  // Keysound
                } else {
                    actualSampleId = sampleId + 1001;  // BGM sample
                }

                // Handle note type (lower 2 bits)
                if (noteType % 4 == 0) {
                    // Normal note
                    Note note(lane, timeMs, false, 0);
                    note.customIndex = actualSampleId;
                    info.notes.push_back(note);
                }
                else if (noteType % 4 == 2) {
                    // Hold start
                    holdStart[lane] = timeMs;
                    holdSampleId[lane] = actualSampleId;
                }
                else if (noteType % 4 == 3) {
                    // Hold end
                    if (holdStart[lane] >= 0) {
                        Note note(lane, holdStart[lane], true, timeMs);
                        note.customIndex = holdSampleId[lane];
                        info.notes.push_back(note);
                        holdStart[lane] = -1;
                    }
                }
            }

            // Channel 9+: background music / auto-play sounds
            if (channel >= 9 && sampleId != 0) {
                int64_t timeMs = measureToMs(measure, position, baseBpm, bpmChanges);

                // Determine actual sample ID based on noteType
                // noteType == 0/2: keysound, noteType == 4/6: BGM sample
                // Store both possible IDs, Game.cpp will pick the one that exists
                int keysoundId = sampleId + 1;
                int bgmId = sampleId + 1001;

                StoryboardSample bgm;
                bgm.time = timeMs;
                bgm.filename = "";
                bgm.volume = 100;
                // Use fallbackHandle to store the alternative ID
                if (noteType == 0 || noteType == 2) {
                    bgm.sampleHandle = keysoundId;
                    bgm.fallbackHandle = bgmId;  // Store BGM ID as fallback
                } else {
                    bgm.sampleHandle = bgmId;
                    bgm.fallbackHandle = keysoundId;  // Store keysound ID as fallback
                }
                info.storyboardSamples.push_back(bgm);
            }
        }
    }

    // Sort notes by time
    std::sort(info.notes.begin(), info.notes.end(),
        [](const Note& a, const Note& b) {
            return a.time < b.time;
        });

    // Add base timing point
    TimingPoint tp;
    tp.time = 0;
    tp.beatLength = 60000.0 / baseBpm;
    tp.uninherited = true;
    tp.effectiveBeatLength = tp.beatLength;
    info.timingPoints.push_back(tp);

    // Add BPM change timing points
    for (const auto& change : bpmChanges) {
        TimingPoint bpmTp;
        bpmTp.time = measureToMs(change.first / 192, change.first % 192, baseBpm, bpmChanges);
        bpmTp.beatLength = 60000.0 / change.second;
        bpmTp.uninherited = true;
        bpmTp.effectiveBeatLength = bpmTp.beatLength;
        info.timingPoints.push_back(bpmTp);
    }

    info.totalObjectCount = static_cast<int>(info.notes.size());
    info.endTimeObjectCount = 0;
    for (const auto& note : info.notes) {
        if (note.isHold) info.endTimeObjectCount++;
    }

    return true;
}
