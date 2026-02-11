#include "S3PParser.h"
#include <fstream>
#include <iostream>
#include <cstring>

bool S3PParser::parse(const std::string& path, std::vector<Sample>& samples) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "S3PParser: Failed to open file: " << path << std::endl;
        return false;
    }

    // Read header
    char magic[4];
    uint32_t numFiles;
    file.read(magic, 4);
    file.read(reinterpret_cast<char*>(&numFiles), 4);

    if (strncmp(magic, "S3P0", 4) != 0) {
        std::cerr << "S3PParser: Invalid magic: " << std::string(magic, 4) << std::endl;
        return false;
    }

    samples.clear();
    samples.reserve(numFiles);

    // Read offset table
    for (uint32_t i = 0; i < numFiles; i++) {
        Sample sample;
        file.read(reinterpret_cast<char*>(&sample.offset), 4);
        file.read(reinterpret_cast<char*>(&sample.size), 4);
        samples.push_back(sample);
    }

    // Read each sample's header to get wave offset and size
    for (auto& sample : samples) {
        file.seekg(sample.offset, std::ios::beg);

        char sampleMagic[4];
        uint32_t headerSize;
        int32_t waveSize;

        file.read(sampleMagic, 4);
        file.read(reinterpret_cast<char*>(&headerSize), 4);
        file.read(reinterpret_cast<char*>(&waveSize), 4);

        if (strncmp(sampleMagic, "S3V0", 4) != 0) {
            std::cerr << "S3PParser: Invalid sample magic at offset " << sample.offset << std::endl;
            continue;
        }

        sample.waveOffset = sample.offset + headerSize;
        sample.waveSize = waveSize;
    }

    return true;
}

bool S3PParser::extractSample(const std::string& path, int index, std::vector<uint8_t>& waveData) {
    std::vector<Sample> samples;
    if (!parse(path, samples)) {
        return false;
    }

    if (index < 0 || index >= static_cast<int>(samples.size())) {
        std::cerr << "S3PParser: Invalid sample index: " << index << std::endl;
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    const auto& sample = samples[index];
    file.seekg(sample.waveOffset, std::ios::beg);

    waveData.resize(sample.waveSize);
    file.read(reinterpret_cast<char*>(waveData.data()), sample.waveSize);

    return true;
}

int S3PParser::getSampleCount(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return 0;

    char magic[4];
    uint32_t numFiles;
    file.read(magic, 4);
    file.read(reinterpret_cast<char*>(&numFiles), 4);

    if (strncmp(magic, "S3P0", 4) != 0) {
        return 0;
    }

    return static_cast<int>(numFiles);
}
