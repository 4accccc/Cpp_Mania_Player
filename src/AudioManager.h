#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <SDL3_mixer/SDL_mixer.h>

class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    bool init();
    bool loadMusic(const std::string& filepath, bool loop = true);  // loop: true for game, false for preview
    void play();
    void stop();
    void fadeIn(int ms);   // Fade in over ms milliseconds
    void fadeOut(int ms);  // Fade out over ms milliseconds
    void pause();
    void resume();
    int64_t getPosition() const;
    void setPosition(int64_t ms);
    int64_t getDuration() const;
    bool isPlaying() const;

    void setVolume(int volume);
    int getVolume() const;
    std::vector<std::string> getAudioDevices();
    int getDeviceCount() const;

    // Sample (key sound) support
    int loadSample(const std::string& filepath);  // Returns sample handle, -1 on failure
    void playSample(int handle, int volume = 100);  // volume: 0-100
    void clearSamples();  // Clear all loaded samples
    void setSampleVolume(int volume);  // Master sample volume (0-100)
    int getSampleVolume() const;

private:
    MIX_Mixer* mixer;
    MIX_Audio* audio;
    MIX_Track* track;
    int sampleRate;       // Mixer sample rate
    int audioSampleRate;  // Current audio file's sample rate
    bool initialized;
    int currentVolume;
    int sampleVolume;  // Master volume for samples

    // Sample cache: handle -> MIX_Audio*
    std::unordered_map<int, MIX_Audio*> sampleCache;
    std::vector<MIX_Track*> sampleTracks;  // Tracks for playing samples
    int nextSampleHandle;
    size_t nextTrackIndex;  // Round-robin track selection
    static const int MAX_SAMPLE_TRACKS = 256;  // Max concurrent samples for O2Jam BGM
};
