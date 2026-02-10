#include "KeySoundManager.h"
#include "AudioManager.h"
#include <filesystem>
#include <algorithm>
#include <SDL3/SDL.h>

namespace fs = std::filesystem;

KeySoundManager::KeySoundManager()
    : audioManager(nullptr), timingPointVolume(100) {}

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
    static const std::vector<std::string> extensions = {".wav", ".ogg", ".mp3"};

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

    int handle = audioManager->loadSample(filepath);
    sampleCache[baseName] = handle;

    return handle;
}

void KeySoundManager::preloadKeySounds(std::vector<Note>& notes) {
    for (auto& note : notes) {
        // Load head/normal key sound
        if (!note.filename.empty()) {
            note.sampleHandle = loadSample(note.filename);
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

    audioManager->playSample(handle, volume);
}

void KeySoundManager::clear() {
    sampleCache.clear();
    // Note: actual audio data is managed by AudioManager
}

void KeySoundManager::preloadStoryboardSamples(std::vector<StoryboardSample>& samples) {
    for (auto& sample : samples) {
        if (!sample.filename.empty()) {
            sample.sampleHandle = loadSample(sample.filename);
        }
    }
}

void KeySoundManager::playStoryboardSample(const StoryboardSample& sample) {
    if (!audioManager) return;

    int handle = sample.sampleHandle;

    // If handle is -1 but filename exists, try to load from cache
    if (handle == -1 && !sample.filename.empty()) {
        handle = loadSample(sample.filename);
    }

    if (handle == -1) return;

    audioManager->playSample(handle, sample.volume);
}
