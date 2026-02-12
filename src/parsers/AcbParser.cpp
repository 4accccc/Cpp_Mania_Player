#include "AcbParser.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <set>
#include <cstring>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>

namespace fs = std::filesystem;

std::vector<size_t> AcbParser::findHcaBlocks(const uint8_t* data, size_t size) {
    std::vector<size_t> blocks;
    const uint8_t hcaSig[] = {'H', 'C', 'A', 0x00};

    for (size_t i = 0; i + 4 <= size; i++) {
        if (memcmp(data + i, hcaSig, 4) == 0) {
            blocks.push_back(i);
        }
    }
    return blocks;
}

size_t AcbParser::getHcaSize(const uint8_t* data, size_t offset, size_t totalSize) {
    if (offset + 8 > totalSize) return 0;

    // HCA header: "HCA\0" (4) + version (2) + header_size (2, big-endian)
    uint16_t headerSize = (data[offset + 6] << 8) | data[offset + 7];

    // Find fmt chunk for block count
    size_t fmtPos = 0;
    for (size_t i = offset; i < offset + headerSize && i + 4 <= totalSize; i++) {
        if (memcmp(data + i, "fmt\x00", 4) == 0) {
            fmtPos = i;
            break;
        }
    }
    if (fmtPos == 0 || fmtPos + 12 > totalSize) return headerSize;

    // Block count at fmt+8 (4 bytes, big-endian)
    uint32_t blockCount = (data[fmtPos + 8] << 24) | (data[fmtPos + 9] << 16) |
                          (data[fmtPos + 10] << 8) | data[fmtPos + 11];

    // Find comp chunk for block size
    size_t compPos = 0;
    for (size_t i = offset; i < offset + headerSize && i + 4 <= totalSize; i++) {
        if (memcmp(data + i, "comp", 4) == 0) {
            compPos = i;
            break;
        }
    }
    if (compPos == 0 || compPos + 6 > totalSize) return headerSize;

    // Block size at comp+4 (2 bytes, big-endian)
    uint16_t blockSize = (data[compPos + 4] << 8) | data[compPos + 5];

    return headerSize + (blockCount * blockSize);
}

int64_t AcbParser::getHcaDuration(const uint8_t* data, size_t size) {
    if (size < 16) return 0;

    // Find fmt chunk
    size_t fmtPos = 0;
    for (size_t i = 0; i + 4 <= size && i < 256; i++) {
        if (memcmp(data + i, "fmt\x00", 4) == 0) {
            fmtPos = i;
            break;
        }
    }
    if (fmtPos == 0 || fmtPos + 12 > size) return 0;

    // Sample rate: bits 4-23 of bytes 5-7 (big-endian, top 4 bits are channels)
    uint32_t sampleRate = ((data[fmtPos + 5] & 0x0F) << 16) |
                          (data[fmtPos + 6] << 8) | data[fmtPos + 7];

    // Block count at fmt+8
    uint32_t blockCount = (data[fmtPos + 8] << 24) | (data[fmtPos + 9] << 16) |
                          (data[fmtPos + 10] << 8) | data[fmtPos + 11];

    // Each block is 1024 samples
    if (sampleRate == 0) return 0;
    int64_t totalSamples = blockCount * 1024LL;
    return (totalSamples * 1000) / sampleRate;
}

bool AcbParser::parse(const std::string& path, std::vector<HcaData>& hcaFiles) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;

    size_t fileSize = file.tellg();
    file.seekg(0);

    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();

    auto blocks = findHcaBlocks(data.data(), fileSize);
    if (blocks.empty()) return false;

    hcaFiles.clear();
    for (size_t i = 0; i < blocks.size(); i++) {
        size_t offset = blocks[i];
        size_t size = getHcaSize(data.data(), offset, fileSize);
        if (size == 0) continue;

        HcaData hca;
        hca.data.assign(data.begin() + offset, data.begin() + offset + size);
        hca.cueName = std::to_string(i);
        hca.duration = getHcaDuration(hca.data.data(), hca.data.size());
        hcaFiles.push_back(std::move(hca));
    }

    return !hcaFiles.empty();
}

bool AcbParser::extractToDir(const std::string& acbPath, const std::string& outputDir) {
    std::vector<HcaData> hcaFiles;
    if (!parse(acbPath, hcaFiles)) return false;

    fs::create_directories(outputDir);

    for (size_t i = 0; i < hcaFiles.size(); i++) {
        std::string outPath = outputDir + "/" + std::to_string(i) + ".hca";
        std::ofstream out(outPath, std::ios::binary);
        if (out) {
            out.write(reinterpret_cast<const char*>(hcaFiles[i].data.data()),
                      hcaFiles[i].data.size());
        }
    }
    return true;
}

int AcbParser::findBgmIndex(const std::vector<HcaData>& hcaFiles) {
    int bgmIndex = -1;
    int64_t maxDuration = 0;

    for (size_t i = 0; i < hcaFiles.size(); i++) {
        if (hcaFiles[i].duration > maxDuration) {
            maxDuration = hcaFiles[i].duration;
            bgmIndex = static_cast<int>(i);
        }
    }
    return bgmIndex;
}

std::vector<AcbCueInfo> AcbParser::extractCueNames(const std::string& path) {
    std::vector<AcbCueInfo> cues;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return cues;

    size_t fileSize = file.tellg();
    file.seekg(0);

    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();

    // First, count HCA blocks to know how many cue names we need
    auto hcaBlocks = findHcaBlocks(data.data(), fileSize);
    size_t hcaCount = hcaBlocks.size();
    if (hcaCount == 0) return cues;

    // Find the first HCA block position - cue names are before this
    size_t firstHcaPos = hcaBlocks[0];

    // Blacklist of metadata strings
    std::set<std::string> blacklist = {
        "CueName", "CueIndex", "CueTable", "WaveformTable", "SynthTable",
        "CommandTable", "TrackTable", "SequenceTable", "BlockTable",
        "AisacTable", "GraphTable", "GlobalAisacTable", "OutsideLinkTable",
        "StringValueTable", "Header", "AwbFile", "StreamAwbHash"
    };

    std::vector<std::string> names;
    std::string current;

    // Scan from 0x4000 to first HCA position (cue names are in header region)
    size_t startOffset = std::min((size_t)0x4000, fileSize);
    size_t endOffset = std::min(firstHcaPos, fileSize);

    for (size_t i = startOffset; i < endOffset; i++) {
        uint8_t c = data[i];
        if (c >= 32 && c < 127 && c != '\\' && c != '/') {
            current += static_cast<char>(c);
        } else if (c == 0 && current.length() >= 3 && current.length() <= 30) {
            bool valid = true;
            for (char ch : current) {
                if (!isalnum(ch) && ch != '_') {
                    valid = false;
                    break;
                }
            }
            if (valid && current[0] != '@' &&
                blacklist.find(current) == blacklist.end() &&
                current.find("Table") == std::string::npos) {
                names.push_back(current);
            }
            current.clear();
        } else {
            current.clear();
        }
    }

    // Only use up to hcaCount cue names
    size_t count = std::min(names.size(), hcaCount);
    for (size_t i = 0; i < count; i++) {
        AcbCueInfo cue;
        cue.name = names[i];
        cue.hcaIndex = static_cast<int>(i);
        cues.push_back(cue);
    }

    return cues;
}

bool AcbParser::convertHcaToWav(const std::string& hcaPath, const std::string& wavPath) {
    // Use FFmpeg to convert HCA to WAV
    std::string cmd = "ffmpeg -y -i \"" + hcaPath + "\" -acodec pcm_s16le \"" + wavPath + "\" 2>nul";
    int result = system(cmd.c_str());
    return result == 0 && fs::exists(wavPath);
}

// Parallel conversion helper: worker threads pull jobs from a shared queue
static void convertWorker(std::queue<std::pair<std::string, std::string>>& jobs,
                          std::mutex& jobMutex) {
    while (true) {
        std::pair<std::string, std::string> job;
        {
            std::lock_guard<std::mutex> lock(jobMutex);
            if (jobs.empty()) return;
            job = jobs.front();
            jobs.pop();
        }
        AcbParser::convertHcaToWav(job.first, job.second);
        // Remove HCA file after conversion
        fs::remove(job.first);
    }
}

bool AcbParser::extractAndConvert(const std::string& acbPath, const std::string& outputDir) {
    std::vector<HcaData> hcaFiles;
    if (!parse(acbPath, hcaFiles)) return false;

    fs::create_directories(outputDir);

    // Find BGM index (longest audio)
    int bgmIndex = findBgmIndex(hcaFiles);

    // Phase 1: Write all HCA files to disk
    std::queue<std::pair<std::string, std::string>> jobs;
    for (size_t i = 0; i < hcaFiles.size(); i++) {
        std::string baseName = std::to_string(i);
        std::string hcaPath = outputDir + "/" + baseName + ".hca";
        std::string wavPath = outputDir + "/" + baseName + ".wav";

        std::ofstream out(hcaPath, std::ios::binary);
        if (out) {
            out.write(reinterpret_cast<const char*>(hcaFiles[i].data.data()),
                      hcaFiles[i].data.size());
            out.close();
            jobs.push({hcaPath, wavPath});
        }
    }

    // Phase 2: Parallel FFmpeg conversion
    unsigned int maxThreads = std::max(1u, std::thread::hardware_concurrency());
    unsigned int numThreads = std::min(maxThreads, static_cast<unsigned int>(jobs.size()));
    std::mutex jobMutex;

    std::vector<std::thread> workers;
    for (unsigned int t = 0; t < numThreads; t++) {
        workers.emplace_back(convertWorker, std::ref(jobs), std::ref(jobMutex));
    }
    for (auto& w : workers) {
        w.join();
    }

    // Create BGM copy for easy access
    if (bgmIndex >= 0) {
        std::string bgmWav = outputDir + "/" + std::to_string(bgmIndex) + ".wav";
        std::string bgmLink = outputDir + "/bgm.wav";
        if (fs::exists(bgmWav)) {
            fs::copy_file(bgmWav, bgmLink, fs::copy_options::overwrite_existing);
        }
    }

    return true;
}

bool AcbParser::extractAndConvert(const std::string& acbPath, const std::string& outputDir,
                                  const std::vector<std::string>& cueNames) {
    std::vector<HcaData> hcaFiles;
    if (!parse(acbPath, hcaFiles)) return false;

    fs::create_directories(outputDir);

    // Find BGM index (longest audio)
    int bgmIndex = findBgmIndex(hcaFiles);

    // Phase 1: Write all HCA files to disk
    std::queue<std::pair<std::string, std::string>> jobs;
    for (size_t i = 0; i < hcaFiles.size(); i++) {
        std::string baseName = (i < cueNames.size()) ? cueNames[i] : std::to_string(i);
        std::string hcaPath = outputDir + "/" + baseName + ".hca";
        std::string wavPath = outputDir + "/" + baseName + ".wav";

        // Skip if WAV already exists (per-file cache)
        if (fs::exists(wavPath)) continue;

        std::ofstream out(hcaPath, std::ios::binary);
        if (out) {
            out.write(reinterpret_cast<const char*>(hcaFiles[i].data.data()),
                      hcaFiles[i].data.size());
            out.close();
            jobs.push({hcaPath, wavPath});
        }
    }

    // Phase 2: Parallel FFmpeg conversion
    if (!jobs.empty()) {
        unsigned int maxThreads = std::max(1u, std::thread::hardware_concurrency());
        unsigned int numThreads = std::min(maxThreads, static_cast<unsigned int>(jobs.size()));
        std::mutex jobMutex;

        std::cerr << "[MUSYNX] Converting " << jobs.size() << " HCA files with "
                  << numThreads << " threads" << std::endl;

        std::vector<std::thread> workers;
        for (unsigned int t = 0; t < numThreads; t++) {
            workers.emplace_back(convertWorker, std::ref(jobs), std::ref(jobMutex));
        }
        for (auto& w : workers) {
            w.join();
        }
    }

    // Create BGM copy for easy access (skip if same file on case-insensitive systems)
    if (bgmIndex >= 0 && bgmIndex < (int)cueNames.size()) {
        std::string bgmName = cueNames[bgmIndex];
        // Check if cue name is already "BGM" (case-insensitive)
        bool isBgm = (bgmName == "BGM" || bgmName == "bgm" || bgmName == "Bgm");
        if (!isBgm) {
            std::string bgmWav = outputDir + "/" + bgmName + ".wav";
            std::string bgmLink = outputDir + "/bgm.wav";
            if (fs::exists(bgmWav)) {
                std::error_code ec;
                fs::copy_file(bgmWav, bgmLink, fs::copy_options::overwrite_existing, ec);
            }
        }
    }

    return true;
}
