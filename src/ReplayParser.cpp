#include "ReplayParser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <set>

// LZMA decoder
#include "LzmaDec.h"

// Custom allocator for LZMA
static void *SzAlloc(ISzAllocPtr p, size_t size) { (void)p; return malloc(size); }
static void SzFree(ISzAllocPtr p, void *address) { (void)p; free(address); }
static const ISzAlloc g_Alloc = { SzAlloc, SzFree };

uint8_t ReplayParser::readByte(const uint8_t* data, size_t& offset) {
    return data[offset++];
}

int16_t ReplayParser::readInt16(const uint8_t* data, size_t& offset) {
    int16_t value = data[offset] | (data[offset + 1] << 8);
    offset += 2;
    return value;
}

int32_t ReplayParser::readInt32(const uint8_t* data, size_t& offset) {
    int32_t value = data[offset] | (data[offset + 1] << 8) |
                    (data[offset + 2] << 16) | (data[offset + 3] << 24);
    offset += 4;
    return value;
}

int64_t ReplayParser::readInt64(const uint8_t* data, size_t& offset) {
    int64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value |= ((int64_t)data[offset + i]) << (i * 8);
    }
    offset += 8;
    return value;
}

uint64_t ReplayParser::readULEB128(const uint8_t* data, size_t& offset) {
    uint64_t result = 0;
    int shift = 0;
    uint8_t byte;
    do {
        byte = data[offset++];
        result |= ((uint64_t)(byte & 0x7F)) << shift;
        shift += 7;
    } while (byte & 0x80);
    return result;
}

std::string ReplayParser::readOsuString(const uint8_t* data, size_t& offset) {
    uint8_t exists = readByte(data, offset);
    if (exists == 0x00) {
        return "";
    }
    if (exists != 0x0b) {
        return "";
    }
    uint64_t length = readULEB128(data, offset);
    std::string result((char*)&data[offset], length);
    offset += length;
    return result;
}

bool ReplayParser::decompressLZMA(const uint8_t* compressed, size_t compressedSize,
                                   std::vector<uint8_t>& decompressed) {
    if (compressedSize < 13) return false;

    // LZMA header: 5 bytes props + 8 bytes uncompressed size
    const uint8_t* props = compressed;
    uint64_t uncompressedSize = 0;
    for (int i = 0; i < 8; i++) {
        uncompressedSize |= ((uint64_t)compressed[5 + i]) << (i * 8);
    }

    if (uncompressedSize > 100 * 1024 * 1024) {
        return false;  // Sanity check: max 100MB
    }

    decompressed.resize((size_t)uncompressedSize);

    size_t destLen = (size_t)uncompressedSize;
    size_t srcLen = compressedSize - 13;

    ELzmaStatus status;
    SRes res = LzmaDecode(
        decompressed.data(), &destLen,
        compressed + 13, &srcLen,
        props, 5,
        LZMA_FINISH_END, &status, &g_Alloc
    );

    return res == SZ_OK;
}

void ReplayParser::parseFrames(const std::string& data, std::vector<ReplayFrame>& frames) {
    frames.clear();
    int64_t currentTime = 0;
    int debugCount = 0;
    bool isFirstFrame = true;

    std::istringstream stream(data);
    std::string frame;

    while (std::getline(stream, frame, ',')) {
        if (frame.empty()) continue;

        int64_t w = 0;
        int x = 0;

        size_t pos1 = frame.find('|');
        if (pos1 == std::string::npos) continue;

        w = std::stoll(frame.substr(0, pos1));

        size_t pos2 = frame.find('|', pos1 + 1);
        if (pos2 == std::string::npos) continue;

        // x is a float in osu! replay format
        x = static_cast<int>(std::stof(frame.substr(pos1 + 1, pos2 - pos1 - 1)));

        // w = -12345 is the end marker, skip it
        if (w == -12345) continue;

        // Skip seed frame (first frame with w=0, x=seed value)
        if (isFirstFrame && w == 0) {
            isFirstFrame = false;
            std::cout << "Skipping seed frame: x=" << x << std::endl;
            continue;
        }
        isFirstFrame = false;

        currentTime += w;

        // Skip frames with negative time (before song start)
        if (currentTime < 0) {
            continue;
        }

        frames.push_back({currentTime, x});

        // Debug: print first 5 frames
        if (debugCount < 5) {
            std::cout << "Frame " << debugCount << ": w=" << w << " time=" << currentTime << " keys=" << x << std::endl;
            debugCount++;
        }
    }

    // Debug: print unique key values
    std::set<int> uniqueKeys;
    for (const auto& f : frames) {
        uniqueKeys.insert(f.keyState);
    }
    std::cout << "Unique key values: ";
    for (int k : uniqueKeys) {
        std::cout << k << " ";
    }
    std::cout << std::endl;

    // Also print last frame time
    if (!frames.empty()) {
        std::cout << "Last frame time: " << frames.back().time << std::endl;
    }
}

bool ReplayParser::parse(const std::string& filepath, ReplayInfo& info) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open replay file: " << filepath << std::endl;
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(fileSize);
    file.read((char*)buffer.data(), fileSize);
    file.close();

    size_t offset = 0;
    const uint8_t* data = buffer.data();

    // Read header
    info.gameMode = readByte(data, offset);
    info.gameVersion = readInt32(data, offset);
    info.beatmapHash = readOsuString(data, offset);
    info.playerName = readOsuString(data, offset);
    info.replayHash = readOsuString(data, offset);

    // osu! format: count300, count100, count50, countGeki(300g), countKatu(200), countMiss
    info.count300 = readInt16(data, offset);
    info.count100 = readInt16(data, offset);
    info.count50 = readInt16(data, offset);
    info.count300g = readInt16(data, offset);  // Geki = 300g in mania
    info.count200 = readInt16(data, offset);   // Katu = 200 in mania
    info.countMiss = readInt16(data, offset);

    info.totalScore = readInt32(data, offset);
    info.maxCombo = readInt16(data, offset);
    info.perfectCombo = readByte(data, offset) != 0;
    info.mods = readInt32(data, offset);

    // Skip life bar graph
    readOsuString(data, offset);

    info.timestamp = readInt64(data, offset);

    // Debug output
    std::cout << "Replay: " << info.playerName << std::endl;
    std::cout << "GameMode: " << info.gameMode << std::endl;
    std::cout << "Mods: " << info.mods << " (0x" << std::hex << info.mods << std::dec << ")" << std::endl;
    std::cout << "300g/300/200/100/50/Miss: " << info.count300g << "/" << info.count300
              << "/" << info.count200 << "/" << info.count100 << "/" << info.count50
              << "/" << info.countMiss << std::endl;

    // Read compressed data
    int32_t compressedLength = readInt32(data, offset);
    if (compressedLength <= 0 || offset + compressedLength > fileSize) {
        std::cerr << "Invalid compressed data length" << std::endl;
        return false;
    }

    std::vector<uint8_t> decompressed;
    if (!decompressLZMA(data + offset, compressedLength, decompressed)) {
        std::cerr << "Failed to decompress replay data" << std::endl;
        return false;
    }

    std::string frameData(decompressed.begin(), decompressed.end());
    parseFrames(frameData, info.frames);

    std::cout << "Replay loaded: " << info.playerName << std::endl;
    std::cout << "Frames: " << info.frames.size() << std::endl;

    return true;
}
