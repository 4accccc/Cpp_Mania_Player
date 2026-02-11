#pragma once
#define NOMINMAX
#include <string>
#include <vector>
#include <unordered_map>
#include <bass.h>
#include <bass_fx.h>

class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    bool init();
    bool loadMusic(const std::string& filepath, bool loop = true);

    void play();
    void stop();
    void fadeIn(int ms);
    void fadeOut(int ms);
    void pause();
    void resume();
    int64_t getPosition() const;
    int64_t getRawPosition() const { return getPosition(); }  // Same as getPosition with BASS
    void setPosition(int64_t ms);
    int64_t getDuration() const;
    bool isPlaying() const;

    void setVolume(int volume);
    int getVolume() const;
    std::vector<std::string> getAudioDevices();
    int getDeviceCount() const;

    // Sample (key sound) support
    int loadSample(const std::string& filepath);
    int loadSampleFromMemory(const void* data, size_t size);
    void playSample(int handle, int volume = 100);
    void pauseAllSamples();
    void resumeAllSamples();
    void stopAllSamples();
    void clearSamples();
    void setSampleVolume(int volume);
    int getSampleVolume() const;
    void warmupSamples();  // Pre-play samples at zero volume to initialize BASS buffers

    // Playback speed control (for DT/HT mods)
    void setPlaybackRate(float rate);
    float getPlaybackRate() const;
    void setChangePitch(bool change);
    bool getChangePitch() const;

private:
    HSTREAM decodeStream;   // Decode stream (source)
    HSTREAM tempoStream;    // Tempo stream (for playback)
    bool initialized;
    int currentVolume;
    int sampleVolume;
    float playbackRate;
    bool changePitch;
    bool loopMusic;

    // Sample cache: handle -> HSAMPLE
    std::unordered_map<int, HSAMPLE> sampleCache;
    int nextSampleHandle;

#ifndef _WIN32
    // Linux: FFmpeg transcoding for unsupported formats
    HSAMPLE loadSampleWithFFmpeg(const void* data, size_t size);
#endif
};
