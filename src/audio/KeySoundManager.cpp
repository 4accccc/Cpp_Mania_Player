#include "KeySoundManager.h"
#include "AudioManager.h"
#include "S3PParser.h"
#include "2dxSoundParser.h"
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <cstring>
#include <SDL3/SDL.h>

namespace fs = std::filesystem;

KeySoundManager::KeySoundManager()
    : audioManager(nullptr), timingPointVolume(100), keysoundVolume(100) {
}

KeySoundManager::~KeySoundManager() {
    clear();
}

std::string KeySoundManager::removeExtension(const std::string& filename) {
    size_t pos = filename.rfind('.');
    if (pos != std::string::npos) {
        return filename.substr(0, pos);
    }
    return filename;
}

std::string KeySoundManager::findAudioFile(const std::string& baseName) {
    static const std::vector<std::string> extensions = {".wav", ".ogg", ".mp3", ".ssf"};

    // Check if baseName is already a full path (contains directory separator)
    if (baseName.find('/') != std::string::npos || baseName.find('\\') != std::string::npos) {
        // Try with extensions first
        for (const auto& ext : extensions) {
            std::string fullPath = baseName + ext;
            if (fs::exists(fullPath)) {
                return fullPath;
            }
        }
        // Try as-is (might already have extension)
        if (fs::exists(baseName)) {
            return baseName;
        }
        return "";
    }

    for (const auto& ext : extensions) {
        fs::path path = fs::path(beatmapDir) / (baseName + ext);
        if (fs::exists(path)) {
            return path.string();
        }
    }

    // Try with original name (might already have extension)
    fs::path path = fs::path(beatmapDir) / baseName;
    if (fs::exists(path)) {
        return path.string();
    }

    return "";
}

int KeySoundManager::loadSample(const std::string& filename) {
    if (!audioManager || filename.empty()) {
        return -1;
    }

    std::string baseName = removeExtension(filename);

    // Check cache first
    auto it = sampleCache.find(baseName);
    if (it != sampleCache.end()) {
        return it->second;
    }

    // Find and load the file
    std::string filepath = findAudioFile(baseName);
    if (filepath.empty()) {
        sampleCache[baseName] = -1;
        return -1;
    }

    int handle = -1;

    // SSF: EZ2AC custom format (16-byte header + raw PCM), convert to WAV in memory
    std::string ext = fs::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".ssf") {
        std::ifstream ssfFile(filepath, std::ios::binary);
        if (!ssfFile) { sampleCache[baseName] = -1; return -1; }
        std::vector<uint8_t> ssfData((std::istreambuf_iterator<char>(ssfFile)),
                                      std::istreambuf_iterator<char>());
        ssfFile.close();
        if (ssfData.size() <= 16) { sampleCache[baseName] = -1; return -1; }

        // SSF header: [0]format(u16) [2]sampleRate(u16) [8]channels(u16)
        //              [10]blockAlign(u16) [12]bitsPerSample(u16)
        uint16_t rawFmt, rawRate, rawCh, rawBlockAlign, rawBits;
        memcpy(&rawFmt, &ssfData[0], 2);
        memcpy(&rawRate, &ssfData[2], 2);
        memcpy(&rawCh, &ssfData[8], 2);
        memcpy(&rawBlockAlign, &ssfData[10], 2);
        memcpy(&rawBits, &ssfData[12], 2);

        uint32_t sampleRate32 = rawRate ? rawRate : 44100;
        uint16_t channels = rawCh ? rawCh : 1;
        uint16_t bitsPerSample = rawBits ? rawBits : 16;
        uint32_t rawPcmSize = (uint32_t)(ssfData.size() - 16);
        const uint8_t* rawPcm = &ssfData[16];

        // Format=2 mono: data is 32-bit per sample, actual audio in upper 16 bits.
        // Convert to standard 16-bit PCM by extracting high word of each int32.
        std::vector<uint8_t> convertedPcm;
        uint32_t pcmSize = rawPcmSize;
        const uint8_t* pcmPtr = rawPcm;

        int bytesPerSample = rawBlockAlign ? (rawBlockAlign / (channels ? channels : 1)) : (bitsPerSample / 8);
        if (rawFmt == 2 && bytesPerSample == 4) {
            // 32-bit stored samples -> extract upper 16 bits
            uint32_t sampleCount = rawPcmSize / (4 * channels);
            convertedPcm.resize(sampleCount * 2 * channels);
            for (uint32_t i = 0; i < sampleCount * channels; i++) {
                // Upper 16 bits of each 32-bit LE value = bytes at offset +2
                uint16_t hi;
                memcpy(&hi, rawPcm + i * 4 + 2, 2);
                memcpy(&convertedPcm[i * 2], &hi, 2);
            }
            pcmPtr = convertedPcm.data();
            pcmSize = (uint32_t)convertedPcm.size();
            bitsPerSample = 16;
        }

        uint16_t blockAlign = channels * (bitsPerSample / 8);
        uint32_t byteRate = sampleRate32 * blockAlign;

        // Build WAV in memory: 44-byte header + PCM data
        std::vector<uint8_t> wav(44 + pcmSize, 0);
        memcpy(&wav[0], "RIFF", 4);
        uint32_t riffSize = 36 + pcmSize;
        memcpy(&wav[4], &riffSize, 4);
        memcpy(&wav[8], "WAVEfmt ", 8);
        uint32_t fmtSize = 16;
        memcpy(&wav[16], &fmtSize, 4);
        uint16_t audioFmt = 1; // PCM
        memcpy(&wav[20], &audioFmt, 2);
        memcpy(&wav[22], &channels, 2);
        memcpy(&wav[24], &sampleRate32, 4);
        memcpy(&wav[28], &byteRate, 4);
        memcpy(&wav[32], &blockAlign, 2);
        memcpy(&wav[34], &bitsPerSample, 2);
        memcpy(&wav[36], "data", 4);
        memcpy(&wav[40], &pcmSize, 4);
        memcpy(&wav[44], pcmPtr, pcmSize);

        handle = audioManager->loadSampleFromMemory(wav.data(), wav.size());
    } else {
        handle = audioManager->loadSample(filepath);
    }

    sampleCache[baseName] = handle;

    return handle;
}

bool KeySoundManager::loadS3PSamples(const std::string& s3pPath) {
    if (!audioManager) return false;

    std::vector<S3PParser::Sample> samples;
    if (!S3PParser::parse(s3pPath, samples)) {
        return false;
    }

    std::ifstream file(s3pPath, std::ios::binary);
    if (!file) return false;

    s3pSampleCache.clear();

    for (size_t i = 0; i < samples.size(); i++) {
        const auto& sample = samples[i];
        if (sample.waveSize <= 0) continue;

        std::vector<uint8_t> waveData(sample.waveSize);
        file.seekg(sample.waveOffset, std::ios::beg);
        file.read(reinterpret_cast<char*>(waveData.data()), sample.waveSize);

        int handle = audioManager->loadSampleFromMemory(waveData.data(), waveData.size());
        if (handle >= 0) {
            // Sample IDs in chart files are 1-based, so store with i+1
            s3pSampleCache[static_cast<int>(i) + 1] = handle;
        }
    }

    std::cout << "KeySoundManager: Loaded " << s3pSampleCache.size()
              << " samples from S3P" << std::endl;
    return true;
}

bool KeySoundManager::load2DXSamples(const std::string& twoDxPath) {
    if (!audioManager) return false;

    std::vector<TwoDxParser::Sample> samples;
    if (!TwoDxParser::parse(twoDxPath, samples)) {
        return false;
    }

    std::ifstream file(twoDxPath, std::ios::binary);
    if (!file) return false;

    // Don't clear cache - may have S3P samples loaded already
    // s3pSampleCache.clear();

    int loadedCount = 0;
    for (size_t i = 0; i < samples.size(); i++) {
        const auto& sample = samples[i];
        if (sample.waveSize <= 0) continue;

        std::vector<uint8_t> waveData(sample.waveSize);
        file.seekg(sample.waveOffset, std::ios::beg);
        file.read(reinterpret_cast<char*>(waveData.data()), sample.waveSize);

        int handle = audioManager->loadSampleFromMemory(waveData.data(), waveData.size());
        if (handle >= 0) {
            // Sample IDs in chart files are 1-based
            s3pSampleCache[static_cast<int>(i) + 1] = handle;
            loadedCount++;
        }
    }

    std::cout << "KeySoundManager: Loaded " << loadedCount
              << " samples from 2DX" << std::endl;
    return loadedCount > 0;
}

std::string KeySoundManager::constructHitsoundName(SampleSet ss, const char* hitType, int customIndex) {
    const char* setName = "normal";
    if (ss == SampleSet::Soft) setName = "soft";
    else if (ss == SampleSet::Drum) setName = "drum";
    // Construct: {setName}-{hitType}{customIndex}
    std::string name = std::string(setName) + "-" + hitType;
    if (customIndex > 1) {
        name += std::to_string(customIndex);
    }
    return name;
}

void KeySoundManager::preloadKeySounds(std::vector<Note>& notes) {
    for (auto& note : notes) {
        // Load head/normal key sound
        if (!note.filename.empty()) {
            note.sampleHandle = loadSample(note.filename);
        } else if (note.customIndex > 0 && note.sampleHandle < 0) {
            // osu! hitsound: construct filename from sampleSet + hitSound flags + customIndex
            // Each active hitSound bit maps to a separate file
            SampleSet ss = (note.sampleSet != SampleSet::None) ? note.sampleSet : SampleSet::Normal;
            SampleSet addSs = (note.additions != SampleSet::None) ? note.additions : ss;

            // hitnormal always uses main sampleSet
            note.sampleHandle = loadSample(constructHitsoundName(ss, "hitnormal", note.customIndex));

            // Additional sounds use additions sampleSet
            if (note.hasWhistle) {
                int h = loadSample(constructHitsoundName(addSs, "hitwhistle", note.customIndex));
                if (h >= 0 && note.sampleHandle < 0) note.sampleHandle = h;
            }
            if (note.hasFinish) {
                int h = loadSample(constructHitsoundName(addSs, "hitfinish", note.customIndex));
                if (h >= 0 && note.sampleHandle < 0) note.sampleHandle = h;
            }
            if (note.hasClap) {
                int h = loadSample(constructHitsoundName(addSs, "hitclap", note.customIndex));
                if (h >= 0 && note.sampleHandle < 0) note.sampleHandle = h;
            }
        }

        // Load tail key sound for hold notes
        if (note.isHold && !note.tailFilename.empty()) {
            note.tailSampleHandle = loadSample(note.tailFilename);
        }
    }
}

void KeySoundManager::playKeySound(const Note& note, bool isTail) {
    if (!audioManager) return;

    int handle = isTail ? note.tailSampleHandle : note.sampleHandle;
    int volume = isTail ? note.tailVolume : note.volume;
    const std::string& filename = isTail ? note.tailFilename : note.filename;

    // Try S3P sample cache first (IIDX format uses customIndex)
    if (handle == -1 && note.customIndex >= 0) {
        auto it = s3pSampleCache.find(note.customIndex);
        if (it != s3pSampleCache.end()) {
            handle = it->second;
        }
    }

    // If handle is -1 but filename exists, try to load from cache
    if (handle == -1 && !filename.empty()) {
        handle = loadSample(filename);
    }

    // If still no sample, skip
    if (handle == -1) {
        return;
    }

    // Use timing point volume if note volume is 0
    if (volume == 0) {
        volume = timingPointVolume;
    }

    // Apply global keysound volume (perceptual curve for natural feel)
    float kvol = keysoundVolume / 100.0f;
    volume = (int)(volume * kvol * kvol);

    // Play keysound (osu! stable lets keysounds overlap, no per-lane stop)
    audioManager->playSample(handle, volume);
}

void KeySoundManager::clear() {
    sampleCache.clear();
    s3pSampleCache.clear();
    // Note: actual audio data is managed by AudioManager
}

void KeySoundManager::preloadStoryboardSamples(std::vector<StoryboardSample>& samples) {
    for (auto& sample : samples) {
        if (!sample.filename.empty()) {
            sample.sampleHandle = loadSample(sample.filename);
        }
    }
}

void KeySoundManager::playStoryboardSample(const StoryboardSample& sample, int64_t offsetMs) {
    if (!audioManager) return;

    int handle = sample.sampleHandle;

    // Try S3P sample cache first (IIDX format uses customIndex)
    if (handle == -1 && sample.customIndex >= 0) {
        auto it = s3pSampleCache.find(sample.customIndex);
        if (it != s3pSampleCache.end()) {
            handle = it->second;
        }
    }

    // If handle is -1 but filename exists, try to load from cache
    if (handle == -1 && !sample.filename.empty()) {
        handle = loadSample(sample.filename);
    }

    if (handle == -1) return;

    audioManager->playSample(handle, sample.volume, offsetMs);
}
