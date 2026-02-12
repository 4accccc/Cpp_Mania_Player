#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "Note.h"
#include "OsuParser.h"

class AudioManager;

class KeySoundManager {
public:
    KeySoundManager();
    ~KeySoundManager();

    void setAudioManager(AudioManager* audio) { audioManager = audio; }
    void setBeatmapDirectory(const std::string& dir) { beatmapDir = dir; }

    // Load a sample file, returns handle (-1 on failure)
    int loadSample(const std::string& filename);

    // Load all samples from S3P file (IIDX format)
    bool loadS3PSamples(const std::string& s3pPath);

    // Load all samples from 2DX file (IIDX format)
    bool load2DXSamples(const std::string& twoDxPath);

    // Preload all key sounds for a beatmap
    void preloadKeySounds(std::vector<Note>& notes);

    // Preload storyboard samples
    void preloadStoryboardSamples(std::vector<StoryboardSample>& samples);

    // Play key sound for a note (stops previous keysound on same lane)
    void playKeySound(const Note& note, bool isTail = false);

    // Play a storyboard sample
    void playStoryboardSample(const StoryboardSample& sample, int64_t offsetMs = 0);

    // Clear all loaded samples
    void clear();

    // Get timing point volume at a specific time
    void setTimingPointVolume(int volume) { timingPointVolume = volume; }

private:
    AudioManager* audioManager;
    std::string beatmapDir;

    // Cache: filename (without extension) -> handle
    std::unordered_map<std::string, int> sampleCache;

    // S3P sample cache: sample ID -> handle
    std::unordered_map<int, int> s3pSampleCache;

    // Timing point volume (0-100)
    int timingPointVolume;

    // Helper: remove extension from filename
    std::string removeExtension(const std::string& filename);

    // Helper: find audio file with various extensions
    std::string findAudioFile(const std::string& baseName);

    // Helper: construct osu! hitsound filename from sampleSet + hitSound + customIndex
    static std::string constructHitsoundName(SampleSet ss, const char* hitType, int customIndex);
};
