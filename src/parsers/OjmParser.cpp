#include "OjmParser.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include "stb_vorbis.c"

// Read little-endian values
static uint16_t readU16(const uint8_t* data) {
    return data[0] | (data[1] << 8);
}

static uint32_t readU32(const uint8_t* data) {
    return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

static int16_t readI16(const uint8_t* data) {
    return static_cast<int16_t>(readU16(data));
}

static int32_t readI32(const uint8_t* data) {
    return static_cast<int32_t>(readU32(data));
}

bool OjmParser::isOjmFile(const std::string& filepath) {
    std::filesystem::path p(filepath);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".ojm";
}

std::string OjmParser::getOjmPath(const std::string& ojnPath) {
    std::filesystem::path p(ojnPath);
    p.replace_extension(".ojm");
    if (std::filesystem::exists(p)) {
        return p.string();
    }
    // Try lowercase
    p.replace_extension(".OJM");
    if (std::filesystem::exists(p)) {
        return p.string();
    }
    return "";
}

std::vector<uint8_t> OjmParser::createWavHeader(uint32_t dataSize,
    uint16_t channels, uint32_t sampleRate, uint16_t bitsPerSample) {
    std::vector<uint8_t> header(44);
    uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
    uint16_t blockAlign = channels * bitsPerSample / 8;

    // RIFF header
    memcpy(&header[0], "RIFF", 4);
    uint32_t fileSize = dataSize + 36;
    memcpy(&header[4], &fileSize, 4);
    memcpy(&header[8], "WAVE", 4);

    // fmt chunk
    memcpy(&header[12], "fmt ", 4);
    uint32_t fmtSize = 16;
    memcpy(&header[16], &fmtSize, 4);
    uint16_t audioFormat = 1;  // PCM
    memcpy(&header[20], &audioFormat, 2);
    memcpy(&header[22], &channels, 2);
    memcpy(&header[24], &sampleRate, 4);
    memcpy(&header[28], &byteRate, 4);
    memcpy(&header[32], &blockAlign, 2);
    memcpy(&header[34], &bitsPerSample, 2);

    // data chunk
    memcpy(&header[36], "data", 4);
    memcpy(&header[40], &dataSize, 4);

    return header;
}

// Nami XOR decryption for M30 format
void OjmParser::decodeNami(std::vector<uint8_t>& data) {
    const char* key = "nami";
    for (size_t i = 0; i + 3 < data.size(); i += 4) {
        data[i + 0] ^= key[0];
        data[i + 1] ^= key[1];
        data[i + 2] ^= key[2];
        data[i + 3] ^= key[3];
    }
}

// OMC rearrange table
static const uint8_t REARRANGE_TABLE[] = {
    0x10, 0x0E, 0x02, 0x09, 0x04, 0x00, 0x07, 0x01,
    0x06, 0x08, 0x0F, 0x0A, 0x05, 0x0C, 0x03, 0x0D,
    0x0B, 0x07, 0x02, 0x0A, 0x0B, 0x03, 0x05, 0x0D,
    0x08, 0x04, 0x00, 0x0C, 0x06, 0x0F, 0x0E, 0x10,
    0x01, 0x09, 0x0C, 0x0D, 0x03, 0x00, 0x06, 0x09,
    0x0A, 0x01, 0x07, 0x08, 0x10, 0x02, 0x0B, 0x0E,
    0x04, 0x0F, 0x05, 0x08, 0x03, 0x04, 0x0D, 0x06,
    0x05, 0x0B, 0x10, 0x02, 0x0C, 0x07, 0x09, 0x0A,
    0x0F, 0x0E, 0x00, 0x01, 0x0F, 0x02, 0x0C, 0x0D,
    0x00, 0x04, 0x01, 0x05, 0x07, 0x03, 0x09, 0x10,
    0x06, 0x0B, 0x0A, 0x08, 0x0E
};

// OMC ACC_XOR key
static const uint8_t ACC_XOR_KEY[] = {
    0x10, 0x2F, 0x4E, 0x6D, 0x8C, 0xAB, 0xCA, 0xE9,
    0x08, 0x27, 0x46, 0x65, 0x84, 0xA3, 0xC2, 0xE1
};

// Rearrange OMC WAV data
static void rearrangeOMC(std::vector<uint8_t>& data) {
    if (data.size() < 4) return;

    size_t blockSize = data.size() / 17;
    std::vector<uint8_t> result(data.size());

    size_t srcPos = 0;
    for (int block = 0; block < 17; block++) {
        size_t dstBlock = REARRANGE_TABLE[block];
        size_t dstPos = dstBlock * blockSize;
        size_t copySize = blockSize;

        // Last block may be larger
        if (block == 16) {
            copySize = data.size() - srcPos;
        }

        if (dstPos + copySize <= result.size()) {
            memcpy(&result[dstPos], &data[srcPos], copySize);
        }
        srcPos += copySize;
    }

    data = std::move(result);
}

// ACC_XOR decryption for OMC WAV data
static void accXorOMC(std::vector<uint8_t>& data) {
    uint8_t acc = 0;
    for (size_t i = 0; i < data.size(); i++) {
        uint8_t temp = data[i];
        data[i] = temp ^ ACC_XOR_KEY[i % 16] ^ acc;
        acc = temp;
    }
}

// Parse M30 format
bool OjmParser::parseM30(const std::vector<uint8_t>& data, OjmInfo& info) {
    if (data.size() < 28) return false;

    info.format = "M30";

    // Read header
    int32_t fileVersion = readI32(&data[4]);
    int32_t encryptionFlag = readI32(&data[8]);
    int32_t sampleCount = readI32(&data[12]);
    int32_t samplesOffset = readI32(&data[16]);
    int32_t payloadSize = readI32(&data[20]);

    (void)fileVersion;
    (void)sampleCount;
    (void)payloadSize;

    size_t pos = samplesOffset;

    // Read samples
    while (pos + 52 <= data.size()) {
        // Sample header (52 bytes)
        char sampleName[33] = {0};
        memcpy(sampleName, &data[pos], 32);
        int32_t sampleSize = readI32(&data[pos + 32]);
        int16_t codecCode = readI16(&data[pos + 36]);
        // int16_t unkFixed = readI16(&data[pos + 38]);
        // int32_t unkMusicFlag = readI32(&data[pos + 40]);
        int16_t ref = readI16(&data[pos + 44]);
        // int16_t unkZero = readI16(&data[pos + 46]);
        // int32_t pcmSamples = readI32(&data[pos + 48]);

        pos += 52;

        if (sampleSize <= 0 || pos + sampleSize > data.size()) {
            break;
        }

        // Extract sample data
        std::vector<uint8_t> sampleData(data.begin() + pos, data.begin() + pos + sampleSize);
        pos += sampleSize;

        // Decrypt if needed (nami encryption = 16)
        if (encryptionFlag == 16) {
            decodeNami(sampleData);
        }

        // Create sample entry
        OjmSample sample;
        sample.isOgg = true;
        sample.data = std::move(sampleData);

        // codecCode: 0 = background (M###), 5 = keysound (W###)
        // Match converter logic: BGM = ref + 1002, keysound = ref + 2
        if (codecCode == 0) {
            sample.id = ref + 1002;  // BGM samples
        } else {
            sample.id = ref + 2;  // Keysound samples
        }

        info.samples[sample.id] = std::move(sample);
    }

    return true;
}

// Parse OMC/OJM format
bool OjmParser::parseOMC(const std::vector<uint8_t>& data, OjmInfo& info) {
    if (data.size() < 20) return false;

    info.format = "OMC";

    // Read header (20 bytes)
    int16_t wavCount = readI16(&data[4]);
    int16_t oggCount = readI16(&data[6]);
    int32_t wavStart = readI32(&data[8]);
    int32_t oggStart = readI32(&data[12]);
    // int32_t fileSize = readI32(&data[16]);

    (void)wavCount;
    (void)oggCount;

    int sampleId = 2;  // WAV samples (keysounds) start at ID 2

    // Parse WAV section
    size_t pos = wavStart;
    while (pos + 56 <= data.size() && (int32_t)pos < oggStart) {
        // WAV header (56 bytes)
        char sampleName[33] = {0};
        memcpy(sampleName, &data[pos], 32);
        int16_t audioFormat = readI16(&data[pos + 32]);
        int16_t numChannels = readI16(&data[pos + 34]);
        int32_t sampleRate = readI32(&data[pos + 36]);
        // int32_t bitRate = readI32(&data[pos + 40]);
        // int16_t blockAlign = readI16(&data[pos + 44]);
        int16_t bitsPerSample = readI16(&data[pos + 46]);
        // int32_t unkData = readI32(&data[pos + 48]);
        int32_t chunkSize = readI32(&data[pos + 52]);

        pos += 56;

        (void)audioFormat;

        if (chunkSize <= 0) {
            sampleId++;
            continue;
        }

        if (pos + chunkSize > data.size()) break;

        // Extract and decode WAV data
        std::vector<uint8_t> sampleData(data.begin() + pos, data.begin() + pos + chunkSize);
        pos += chunkSize;

        // Decode: rearrange then ACC_XOR
        rearrangeOMC(sampleData);
        accXorOMC(sampleData);

        // Create WAV with header
        auto wavHeader = createWavHeader(
            (uint32_t)sampleData.size(),
            (uint16_t)numChannels,
            (uint32_t)sampleRate,
            (uint16_t)bitsPerSample
        );

        // Combine header and data
        std::vector<uint8_t> fullWav;
        fullWav.reserve(wavHeader.size() + sampleData.size());
        fullWav.insert(fullWav.end(), wavHeader.begin(), wavHeader.end());
        fullWav.insert(fullWav.end(), sampleData.begin(), sampleData.end());

        OjmSample sample;
        sample.id = sampleId;
        sample.isOgg = false;
        sample.data = std::move(fullWav);

        info.samples[sample.id] = std::move(sample);
        sampleId++;
    }

    // Parse OGG section
    pos = oggStart;
    int oggId = 1002;  // BGM samples start at ID 1002

    while (pos + 36 <= data.size()) {
        // OGG header (36 bytes)
        char sampleName[33] = {0};
        memcpy(sampleName, &data[pos], 32);
        int32_t sampleSize = readI32(&data[pos + 32]);

        pos += 36;

        if (sampleSize <= 0) {
            oggId++;
            continue;
        }

        if (pos + sampleSize > data.size()) break;

        // OGG data is not encrypted
        std::vector<uint8_t> sampleData(data.begin() + pos, data.begin() + pos + sampleSize);
        pos += sampleSize;

        OjmSample sample;
        sample.id = oggId;
        sample.isOgg = true;
        sample.data = std::move(sampleData);

        info.samples[sample.id] = std::move(sample);
        oggId++;
    }

    return true;
}

// Main parse function
bool OjmParser::parse(const std::string& filepath, OjmInfo& info) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open OJM file: " << filepath << std::endl;
        return false;
    }

    // Read entire file
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();

    if (data.size() < 4) {
        std::cerr << "OJM file too small" << std::endl;
        return false;
    }

    // Check signature
    char signature[4];
    memcpy(signature, data.data(), 4);

    if (memcmp(signature, "M30", 3) == 0) {
        return parseM30(data, info);
    } else if (memcmp(signature, "OMC", 3) == 0 || memcmp(signature, "OJM", 3) == 0) {
        return parseOMC(data, info);
    } else {
        std::cerr << "Unknown OJM format: " << std::string(signature, 4) << std::endl;
        return false;
    }
}

// Unused but declared in header
void OjmParser::decryptOMC(std::vector<uint8_t>& data) {
    rearrangeOMC(data);
    accXorOMC(data);
}

void OjmParser::decode0412(std::vector<uint8_t>& data) {
    // Not implemented - rarely used
    (void)data;
}

// Parse OJN to get note events for preview generation (HD difficulty)
std::vector<OjmNoteEvent> OjmParser::parseOjnNotes(const std::string& ojnPath) {
    std::vector<OjmNoteEvent> events;
    std::ifstream file(ojnPath, std::ios::binary);
    if (!file) return events;

    // Read header
    file.seekg(36);  // Skip to BPM
    float bpm;
    file.read(reinterpret_cast<char*>(&bpm), 4);

    // Skip to note offset for HD (index 2)
    file.seekg(168);  // noteOffset[0] position
    int32_t noteOffsets[3];
    for (int i = 0; i < 3; i++) {
        file.read(reinterpret_cast<char*>(&noteOffsets[i]), 4);
    }

    // Use HD difficulty (index 2)
    int32_t startOffset = noteOffsets[2];
    int32_t endOffset = 0;
    file.seekg(180);  // coverOffset position
    file.read(reinterpret_cast<char*>(&endOffset), 4);

    // Read note packages
    file.seekg(startOffset);
    while (file.tellg() < endOffset && file.good()) {
        int32_t measure;
        int16_t channel, eventCount;

        file.read(reinterpret_cast<char*>(&measure), 4);
        file.read(reinterpret_cast<char*>(&channel), 2);
        file.read(reinterpret_cast<char*>(&eventCount), 2);

        if (!file.good()) break;

        for (int evt = 0; evt < eventCount; evt++) {
            int16_t sampleId;
            int8_t pan, noteType;

            file.read(reinterpret_cast<char*>(&sampleId), 2);
            file.read(reinterpret_cast<char*>(&pan), 1);
            file.read(reinterpret_cast<char*>(&noteType), 1);

            // Only process note channels (2-8) and BGM channels (9+)
            if ((channel >= 2 && channel <= 8) || channel >= 9) {
                if (sampleId != 0) {
                    int position = (evt * 192) / eventCount;
                    int64_t timeMs = (int64_t)((measure * 4 + position / 48.0) * 60000.0 / bpm);

                    // Determine actual sample ID based on noteType
                    int actualSampleId;
                    if (noteType == 0 || noteType == 2) {
                        actualSampleId = sampleId + 1;  // Keysound
                    } else {
                        actualSampleId = sampleId + 1001;  // BGM sample
                    }

                    OjmNoteEvent event;
                    event.timeMs = timeMs;
                    event.sampleId = actualSampleId;
                    events.push_back(event);
                }
            }
        }
    }

    // Sort by time
    std::sort(events.begin(), events.end(),
        [](const OjmNoteEvent& a, const OjmNoteEvent& b) {
            return a.timeMs < b.timeMs;
        });

    return events;
}

// Generate preview audio from OJN+OJM
std::string OjmParser::generatePreview(const std::string& ojnPath, int durationMs) {
    // Get OJM path
    std::string ojmPath = getOjmPath(ojnPath);
    if (ojmPath.empty()) return "";

    // Parse OJM
    OjmInfo ojmInfo;
    if (!parse(ojmPath, ojmInfo)) return "";

    // Parse OJN notes
    std::vector<OjmNoteEvent> events = parseOjnNotes(ojnPath);
    if (events.empty()) return "";

    // Output parameters
    const int sampleRate = 44100;
    const int channels = 2;
    const int bitsPerSample = 16;
    const int bytesPerSample = bitsPerSample / 8 * channels;

    // Create output buffer (duration + 2 seconds for tail)
    int totalSamples = (durationMs + 2000) * sampleRate / 1000;
    std::vector<int32_t> mixBuffer(totalSamples * channels, 0);

    // Process each event
    for (const auto& event : events) {
        if (event.timeMs > durationMs) break;

        auto it = ojmInfo.samples.find(event.sampleId);
        if (it == ojmInfo.samples.end()) continue;

        const OjmSample& sample = it->second;

        // Decoded PCM data
        std::vector<int16_t> pcmData;
        int pcmChannels = 0;
        int pcmSampleRate = 0;

        if (sample.isOgg) {
            // Decode OGG using stb_vorbis
            int oggChannels, oggSampleRate;
            short* oggOutput = nullptr;
            int oggSamples = stb_vorbis_decode_memory(
                sample.data.data(), (int)sample.data.size(),
                &oggChannels, &oggSampleRate, &oggOutput);

            if (oggSamples > 0 && oggOutput) {
                pcmData.assign(oggOutput, oggOutput + oggSamples * oggChannels);
                pcmChannels = oggChannels;
                pcmSampleRate = oggSampleRate;
                free(oggOutput);
            }
        } else {
            // Parse WAV
            if (sample.data.size() < 44) continue;
            if (memcmp(sample.data.data(), "RIFF", 4) != 0) continue;

            uint16_t wavChannels = readU16(&sample.data[22]);
            uint32_t wavSampleRate = readU32(&sample.data[24]);
            uint16_t wavBits = readU16(&sample.data[34]);

            if (wavBits != 16) continue;

            // Find data chunk
            size_t dataPos = 12;
            uint32_t dataSize = 0;
            while (dataPos + 8 < sample.data.size()) {
                if (memcmp(&sample.data[dataPos], "data", 4) == 0) {
                    dataSize = readU32(&sample.data[dataPos + 4]);
                    dataPos += 8;
                    break;
                }
                uint32_t chunkSize = readU32(&sample.data[dataPos + 4]);
                dataPos += 8 + chunkSize;
            }
            if (dataSize == 0) continue;

            // Copy PCM data
            size_t numSamples = dataSize / 2;
            pcmData.resize(numSamples);
            for (size_t i = 0; i < numSamples && dataPos + 2 <= sample.data.size(); i++) {
                pcmData[i] = (int16_t)readU16(&sample.data[dataPos]);
                dataPos += 2;
            }
            pcmChannels = wavChannels;
            pcmSampleRate = wavSampleRate;
        }

        if (pcmData.empty() || pcmChannels == 0) continue;

        // Calculate start position in output
        int64_t startSample = event.timeMs * sampleRate / 1000;
        if (startSample < 0) startSample = 0;

        // Mix sample into buffer
        size_t srcIdx = 0;
        int64_t dstPos = startSample * channels;

        while (srcIdx < pcmData.size() && dstPos < (int64_t)mixBuffer.size()) {
            int16_t srcSample = pcmData[srcIdx++];

            if (pcmChannels == 1) {
                // Mono to stereo
                mixBuffer[dstPos] += srcSample;
                mixBuffer[dstPos + 1] += srcSample;
                dstPos += 2;
            } else {
                // Stereo
                mixBuffer[dstPos] += srcSample;
                if (srcIdx < pcmData.size()) {
                    mixBuffer[dstPos + 1] += pcmData[srcIdx++];
                }
                dstPos += 2;
            }
        }
    }

    // Clamp and convert to 16-bit
    std::vector<int16_t> outputData(mixBuffer.size());
    for (size_t i = 0; i < mixBuffer.size(); i++) {
        int32_t val = mixBuffer[i];
        if (val > 32767) val = 32767;
        if (val < -32768) val = -32768;
        outputData[i] = (int16_t)val;
    }

    // Create WAV file
    std::vector<uint8_t> wavHeader = createWavHeader(
        (uint32_t)(outputData.size() * 2), channels, sampleRate, bitsPerSample);

    // Save to temp file
    std::filesystem::path tempDir = std::filesystem::current_path() / "Data" / "Tmp" / "ojm";
    std::filesystem::create_directories(tempDir);

    std::filesystem::path ojnFile(ojnPath);
    std::string previewPath = (tempDir / (ojnFile.stem().string() + "_preview.wav")).string();

    std::ofstream out(previewPath, std::ios::binary);
    if (!out) return "";

    out.write(reinterpret_cast<const char*>(wavHeader.data()), wavHeader.size());
    out.write(reinterpret_cast<const char*>(outputData.data()), outputData.size() * 2);
    out.close();

    return previewPath;
}
