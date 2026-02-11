#include "2dxSoundParser.h"
#include <fstream>
#include <cstring>
#include <iostream>

bool TwoDxParser::parse(const std::string& path, std::vector<Sample>& samples) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    // Read header (72 bytes)
    char magic[16];
    file.read(magic, 16);

    uint32_t headerSize, numFiles;
    file.read(reinterpret_cast<char*>(&headerSize), 4);
    file.read(reinterpret_cast<char*>(&numFiles), 4);

    // Skip padding (48 bytes)
    file.seekg(48, std::ios::cur);

    if (numFiles == 0 || numFiles > 10000) {
        return false;
    }

    // Read file offsets
    std::vector<uint32_t> offsets(numFiles);
    for (uint32_t i = 0; i < numFiles; i++) {
        file.read(reinterpret_cast<char*>(&offsets[i]), 4);
    }

    samples.clear();
    samples.reserve(numFiles);

    // Parse each sound file
    for (uint32_t i = 0; i < numFiles; i++) {
        file.seekg(offsets[i], std::ios::beg);

        char soundMagic[4];
        file.read(soundMagic, 4);

        // Check magic "2DX9"
        if (memcmp(soundMagic, "2DX9", 4) != 0) {
            continue;
        }

        Sample sample;
        sample.offset = offsets[i];

        file.read(reinterpret_cast<char*>(&sample.headerSize), 4);
        file.read(reinterpret_cast<char*>(&sample.waveSize), 4);

        sample.waveOffset = sample.offset + sample.headerSize;

        if (sample.waveSize > 0) {
            samples.push_back(sample);
        }
    }

    std::cout << "TwoDxParser: Found " << samples.size() << " samples" << std::endl;
    return !samples.empty();
}

int TwoDxParser::getSampleCount(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return 0;

    file.seekg(20, std::ios::beg);  // Skip magic(16) + headerSize(4)
    uint32_t numFiles;
    file.read(reinterpret_cast<char*>(&numFiles), 4);

    return static_cast<int>(numFiles);
}
