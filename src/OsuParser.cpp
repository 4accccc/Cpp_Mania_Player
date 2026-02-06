#include "OsuParser.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <SDL3/SDL.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#endif

std::string OsuParser::calculateMD5(const std::string& filepath) {
#ifdef _WIN32
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return "";

    std::vector<char> buffer((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    file.close();

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::string result;

    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
            if (CryptHashData(hHash, (BYTE*)buffer.data(), buffer.size(), 0)) {
                BYTE hash[16];
                DWORD hashLen = 16;
                if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {
                    std::ostringstream oss;
                    for (int i = 0; i < 16; i++) {
                        oss << std::hex << std::setfill('0') << std::setw(2) << (int)hash[i];
                    }
                    result = oss.str();
                }
            }
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
    return result;
#else
    return "";
#endif
}

std::string OsuParser::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

int OsuParser::xToLane(int x, int keyCount) {
    int laneWidth = 512 / keyCount;
    return std::min(x / laneWidth, keyCount - 1);
}

// Helper function to parse extras field for key sound data
static void parseExtras(const std::string& extras, bool isHoldNote,
                        SampleSet& sampleSet, SampleSet& additions,
                        int& customIndex, int& volume, std::string& filename,
                        int64_t& endTime) {
    if (extras.empty()) return;

    std::vector<std::string> parts;
    std::stringstream ss(extras);
    std::string token;
    while (std::getline(ss, token, ':')) {
        // Trim whitespace
        size_t start = token.find_first_not_of(" \t\r\n");
        size_t end = token.find_last_not_of(" \t\r\n");
        if (start != std::string::npos) {
            parts.push_back(token.substr(start, end - start + 1));
        } else {
            parts.push_back("");
        }
    }

    int offset = 0;
    if (isHoldNote && !parts.empty()) {
        // For hold notes, first field is endTime
        try {
            endTime = std::stoll(parts[0]);
        } catch (...) {}
        offset = 1;
    }

    try {
        if (parts.size() > offset + 0 && !parts[offset + 0].empty()) {
            sampleSet = static_cast<SampleSet>(std::stoi(parts[offset + 0]));
        }
        if (parts.size() > offset + 1 && !parts[offset + 1].empty()) {
            additions = static_cast<SampleSet>(std::stoi(parts[offset + 1]));
        }
        if (parts.size() > offset + 2 && !parts[offset + 2].empty()) {
            customIndex = std::stoi(parts[offset + 2]);
        }
        if (parts.size() > offset + 3 && !parts[offset + 3].empty()) {
            volume = std::stoi(parts[offset + 3]);
        }
        if (parts.size() > offset + 4 && !parts[offset + 4].empty()) {
            filename = parts[offset + 4];
        }
    } catch (...) {}
}

bool OsuParser::isMania(const BeatmapInfo& info) {
    return info.mode == 3;
}

bool OsuParser::parse(const std::string& filepath, BeatmapInfo& info) {
    // Calculate MD5 hash first
    info.beatmapHash = calculateMD5(filepath);

    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    std::string line;
    std::string section;
    info.mode = -1;
    info.keyCount = 4;
    info.od = 5.0f;
    info.cs = 5.0f;
    info.ar = 5.0f;
    info.hp = 5.0f;
    info.sliderMultiplier = 1.4f;
    info.totalObjectCount = 0;
    info.endTimeObjectCount = 0;

    // Local timing points for slider duration calculation
    std::vector<TimingPoint> timingPoints;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '/') continue;

        if (line[0] == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        size_t colonPos = line.find(':');
        if (colonPos != std::string::npos && section != "HitObjects") {
            std::string key = trim(line.substr(0, colonPos));
            std::string value = trim(line.substr(colonPos + 1));

            if (section == "General") {
                if (key == "AudioFilename") info.audioFilename = value;
                else if (key == "Mode") info.mode = std::stoi(value);
            }
            else if (section == "Metadata") {
                if (key == "Title") info.title = value;
                else if (key == "Artist") info.artist = value;
                else if (key == "Creator") info.creator = value;
                else if (key == "Version") info.version = value;
            }
            else if (section == "Difficulty") {
                if (key == "CircleSize") {
                    info.cs = std::stof(value);
                    info.keyCount = std::stoi(value);
                }
                else if (key == "OverallDifficulty") info.od = std::stof(value);
                else if (key == "ApproachRate") info.ar = std::stof(value);
                else if (key == "HPDrainRate") info.hp = std::stof(value);
                else if (key == "SliderMultiplier") info.sliderMultiplier = std::stof(value);
            }
        }

        // Parse TimingPoints
        if (section == "TimingPoints") {
            std::stringstream ss(line);
            std::string token;
            std::vector<std::string> parts;
            while (std::getline(ss, token, ',')) {
                parts.push_back(trim(token));
            }
            if (parts.size() >= 2) {
                TimingPoint tp;
                try {
                    tp.time = std::stod(parts[0]);
                    tp.beatLength = std::stod(parts[1]);
                    tp.uninherited = (parts.size() < 7 || parts[6] == "1");
                    timingPoints.push_back(tp);
                } catch (const std::exception& e) {
                    SDL_Log("OsuParser: Failed to parse timing point: %s", line.c_str());
                }
            }
        }

        // Parse Events (Storyboard Sound Samples)
        if (section == "Events" && line.rfind("Sample,", 0) == 0) {
            // Format: Sample,time,layer,"filename",volume
            std::vector<std::string> parts;
            std::stringstream ss(line);
            std::string token;
            while (std::getline(ss, token, ',')) {
                parts.push_back(trim(token));
            }
            if (parts.size() >= 5) {
                try {
                    StoryboardSample sample;
                    sample.time = std::stoll(parts[1]);
                    // Remove quotes from filename
                    std::string fn = parts[3];
                    if (fn.size() >= 2 && fn.front() == '"' && fn.back() == '"') {
                        fn = fn.substr(1, fn.size() - 2);
                    }
                    sample.filename = fn;
                    sample.volume = std::stoi(parts[4]);
                    info.storyboardSamples.push_back(sample);
                } catch (...) {}
            }
        }

        if (section == "HitObjects") {
            std::stringstream ss(line);
            std::string token;
            std::vector<std::string> parts;
            while (std::getline(ss, token, ',')) {
                parts.push_back(trim(token));
            }

            if (parts.size() >= 5) {
                try {
                // Handle fake notes with NaN time (used in SV maps for visual effects)
                bool isFakeNote = false;
                int64_t time = 0;
                if (parts[2] == "NaN" || parts[2] == "nan" || parts[2] == "Infinity" || parts[2] == "-Infinity") {
                    isFakeNote = true;
                    time = -999999;  // Very early time, will be adjusted later for hold notes
                } else {
                    time = std::stoll(parts[2]);
                }
                int x = std::stoi(parts[0]);
                int type = std::stoi(parts[3]);
                int hitsound = (parts.size() >= 5) ? std::stoi(parts[4]) : 0;
                bool hasClap = (hitsound & 8) != 0;    // bit 3
                bool hasFinish = (hitsound & 4) != 0;  // bit 2

                // Count objects for key count calculation
                info.totalObjectCount++;
                // Slider (bit 1) or Spinner (bit 3) = objects with duration
                if ((type & 2) || (type & 8)) {
                    info.endTimeObjectCount++;
                }

                int lane = xToLane(x, info.keyCount);
                bool isHold = false;
                int64_t endTime = 0;
                ObjectType objType = ObjectType::HitCircle;
                int spanCount = 1;
                int segmentDuration = 0;

                // Mania hold note (type & 128)
                if (type & 128) {
                    isHold = true;
                    if (parts.size() >= 6) {
                        size_t colonPos = parts[5].find(':');
                        if (colonPos != std::string::npos) {
                            endTime = std::stoll(parts[5].substr(0, colonPos));
                        } else {
                            endTime = std::stoll(parts[5]);
                        }
                    }
                }
                // Slider (type & 2)
                else if (type & 2) {
                    objType = ObjectType::Slider;
                    isHold = true;
                    if (parts.size() >= 8) {
                        int slides = std::stoi(parts[6]);
                        double length = std::stod(parts[7]);
                        spanCount = slides;

                        // Find timing point for this time
                        double beatLength = 500.0; // default 120 BPM
                        double velocityMultiplier = 1.0;
                        for (const auto& tp : timingPoints) {
                            if (tp.time <= time) {
                                if (tp.uninherited) {
                                    beatLength = tp.beatLength;
                                } else if (tp.beatLength < 0) {
                                    velocityMultiplier = -100.0 / tp.beatLength;
                                }
                            }
                        }

                        // Calculate slider duration (matches stable's calculation)
                        double duration = length * beatLength * spanCount * 0.01 / (info.sliderMultiplier * velocityMultiplier);
                        endTime = time + static_cast<int64_t>(std::floor(duration));
                        segmentDuration = (endTime - time) / spanCount;
                    }
                }
                // Spinner (type & 8)
                else if (type & 8) {
                    objType = ObjectType::Spinner;
                    isHold = true;
                    if (parts.size() >= 6) {
                        endTime = std::stoll(parts[5]);
                    }
                }

                // For fake notes with NaN start time, adjust start time based on end time
                if (isFakeNote && isHold && endTime > 0) {
                    // Set start time to create a long visual LN ending at endTime
                    time = endTime - 10000;  // 10 seconds before end time
                }

                info.notes.emplace_back(lane, time, isHold, endTime);
                info.notes.back().x = static_cast<float>(x);
                info.notes.back().objectType = objType;
                info.notes.back().spanCount = spanCount;
                info.notes.back().segmentDuration = segmentDuration;
                info.notes.back().hasClap = hasClap;
                info.notes.back().hasFinish = hasFinish;
                info.notes.back().isFakeNote = isFakeNote;

                // Parse extras field for key sound data
                if (parts.size() >= 6) {
                    std::string extras = parts[5];
                    // For sliders, extras is at index 10 (if present)
                    if ((type & 2) && parts.size() >= 11) {
                        extras = parts[10];
                    }

                    SampleSet ss = SampleSet::None;
                    SampleSet add = SampleSet::None;
                    int ci = 0, vol = 0;
                    std::string fn;
                    int64_t dummyEnd = endTime;

                    bool isManiaHold = (type & 128) != 0;
                    parseExtras(extras, isManiaHold, ss, add, ci, vol, fn, dummyEnd);

                    // For mania hold notes, endTime was parsed from extras
                    if (isManiaHold && dummyEnd > 0) {
                        info.notes.back().endTime = dummyEnd;
                    }

                    info.notes.back().sampleSet = ss;
                    info.notes.back().additions = add;
                    info.notes.back().customIndex = ci;
                    info.notes.back().volume = vol;
                    info.notes.back().filename = fn;
                }

                // Check if fake note should have fixed position
                // (extreme beatLength after endTime indicates visual-only fake note)
                if (isFakeNote) {
                    for (const auto& tp : timingPoints) {
                        // Check timing points within 10ms after endTime
                        if (tp.time >= endTime && tp.time <= endTime + 10) {
                            // Extreme beatLength (>100000) means this fake note should be fixed
                            if (tp.uninherited && tp.beatLength > 100000) {
                                info.notes.back().fakeNoteShouldFix = true;
                                SDL_Log("Fake note endTime=%lld marked as shouldFix", (long long)endTime);
                                break;
                            }
                        }
                    }
                }
                } catch (const std::exception& e) {
                    SDL_Log("OsuParser: Failed to parse hit object: %s", line.c_str());
                }
            }
        }
    }

    // Calculate effectiveBeatLength for each timing point (for SV calculation)
    // osu! uses beatLength sign to determine red/green line
    // Formula: pixels = 21.0 * speed * time / effectiveBeatLength
    double baseBeatLength = 500.0;  // default 120 BPM
    for (auto& tp : timingPoints) {
        if (tp.beatLength >= 0) {
            // Red line (positive beatLength): set base beat length
            // Clamp to reasonable range to prevent overflow from extreme SV maps
            // Min: 6ms (10000 BPM), Max: 60000ms (1 BPM)
            baseBeatLength = std::clamp(tp.beatLength, 6.0, 60000.0);
            tp.effectiveBeatLength = baseBeatLength;
        } else {
            // Green line (negative beatLength): SV multiplier
            // osu!mania method_5(): clamp(-beatLength, 10, 10000) / 100
            // SV range: 0.1x to 100x
            double sv = -tp.beatLength;
            sv = std::max(10.0, std::min(10000.0, sv));
            double svMultiplier = sv / 100.0;
            tp.effectiveBeatLength = baseBeatLength / svMultiplier;
        }
    }

    // Save timing points to BeatmapInfo
    for (const auto& tp : timingPoints) {
        info.timingPoints.push_back(tp);
    }

    std::sort(info.notes.begin(), info.notes.end(),
        [](const Note& a, const Note& b) { return a.time < b.time; });

    return true;
}
