#include "AudioManager.h"
#include <SDL3/SDL.h>
#include <iostream>

AudioManager::AudioManager()
    : mixer(nullptr), audio(nullptr), track(nullptr), sampleRate(44100), audioSampleRate(44100),
      initialized(false), currentVolume(100), sampleVolume(100), nextSampleHandle(1), nextTrackIndex(0) {}

AudioManager::~AudioManager() {
    clearSamples();
    if (track) MIX_DestroyTrack(track);
    if (audio) MIX_DestroyAudio(audio);
    if (mixer) MIX_DestroyMixer(mixer);
    if (initialized) MIX_Quit();
}

bool AudioManager::init() {
    if (!MIX_Init()) {
        return false;
    }

    SDL_AudioSpec spec;
    spec.freq = sampleRate;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;

    mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!mixer) return false;

    // Get actual sample rate from mixer
    SDL_AudioSpec actualSpec;
    if (MIX_GetMixerFormat(mixer, &actualSpec)) {
        sampleRate = actualSpec.freq;
        std::cout << "Audio sample rate: " << sampleRate << " Hz" << std::endl;
    }

    // Set mixer gain to prevent clipping when multiple samples play simultaneously
    MIX_SetMixerGain(mixer, 0.5f);

    track = MIX_CreateTrack(mixer);
    if (!track) return false;

    initialized = true;
    return true;
}

bool AudioManager::loadMusic(const std::string& filepath, bool loop) {
    if (audio) {
        MIX_DestroyAudio(audio);
    }
    audio = MIX_LoadAudio(mixer, filepath.c_str(), loop);
    if (!audio) {
        std::cerr << "MIX_LoadAudio failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // Get audio file's actual sample rate
    SDL_AudioSpec audioSpec;
    if (MIX_GetAudioFormat(audio, &audioSpec)) {
        audioSampleRate = audioSpec.freq;
        std::cout << "Audio file sample rate: " << audioSampleRate << " Hz" << std::endl;
    } else {
        audioSampleRate = sampleRate;  // Fallback to mixer rate
    }

    MIX_SetTrackAudio(track, audio);
    return true;
}

void AudioManager::play() {
    if (track) {
        MIX_PlayTrack(track, 0);
    }
}

void AudioManager::stop() {
    if (track) {
        MIX_StopTrack(track, 0);
    }
}

void AudioManager::fadeIn(int ms) {
    (void)ms;  // SDL3_mixer doesn't have fade API, just play directly
    if (track) {
        MIX_PlayTrack(track, 0);
    }
}

void AudioManager::fadeOut(int ms) {
    (void)ms;  // SDL3_mixer doesn't have fade API, just stop directly
    if (track) {
        MIX_StopTrack(track, 0);
    }
}

void AudioManager::pause() {
    if (track) {
        MIX_PauseTrack(track);
    }
}

void AudioManager::resume() {
    if (track) {
        MIX_ResumeTrack(track);
    }
}

int64_t AudioManager::getPosition() const {
    if (!track) return 0;
    Sint64 frames = MIX_GetTrackPlaybackPosition(track);
    // Use audio file's sample rate, not mixer's rate
    return (frames * 1000) / audioSampleRate;
}

void AudioManager::setPosition(int64_t ms) {
    if (!track) return;
    // Use audio file's sample rate, not mixer's rate
    Sint64 frames = (ms * audioSampleRate) / 1000;
    MIX_SetTrackPlaybackPosition(track, frames);
}

bool AudioManager::isPlaying() const {
    if (!track) return false;
    return MIX_GetTrackRemaining(track) > 0;
}

int64_t AudioManager::getDuration() const {
    return 0;
}

void AudioManager::setVolume(int volume) {
    currentVolume = volume;
    if (track) {
        MIX_SetTrackGain(track, volume / 100.0f);
    }
}

int AudioManager::getVolume() const {
    return currentVolume;
}

std::vector<std::string> AudioManager::getAudioDevices() {
    std::vector<std::string> devices;
    int count = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
    if (ids) {
        for (int i = 0; i < count; i++) {
            const char* name = SDL_GetAudioDeviceName(ids[i]);
            if (name) {
                devices.push_back(name);
            }
        }
        SDL_free(ids);
    }
    if (devices.empty()) {
        devices.push_back("Default");
    }
    return devices;
}

int AudioManager::getDeviceCount() const {
    int count = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
    if (ids) SDL_free(ids);
    return count > 0 ? count : 1;
}

int AudioManager::loadSample(const std::string& filepath) {
    if (!mixer) return -1;

    MIX_Audio* sampleAudio = MIX_LoadAudio(mixer, filepath.c_str(), false);
    if (!sampleAudio) {
        return -1;
    }

    int handle = nextSampleHandle++;
    sampleCache[handle] = sampleAudio;
    return handle;
}

void AudioManager::playSample(int handle, int volume) {
    if (!mixer) return;

    auto it = sampleCache.find(handle);
    if (it == sampleCache.end() || !it->second) return;

    // Create tracks up to MAX_SAMPLE_TRACKS
    while (sampleTracks.size() < MAX_SAMPLE_TRACKS) {
        MIX_Track* newTrack = MIX_CreateTrack(mixer);
        if (newTrack) {
            sampleTracks.push_back(newTrack);
        } else {
            break;
        }
    }

    if (sampleTracks.empty()) return;

    // Find a free track (not playing) first
    MIX_Track* sampleTrack = nullptr;
    for (size_t i = 0; i < sampleTracks.size(); i++) {
        size_t idx = (nextTrackIndex + i) % sampleTracks.size();
        if (MIX_GetTrackRemaining(sampleTracks[idx]) == 0) {
            sampleTrack = sampleTracks[idx];
            nextTrackIndex = (idx + 1) % sampleTracks.size();
            break;
        }
    }

    // If no free track, use round-robin (will cut off oldest sound)
    if (!sampleTrack) {
        sampleTrack = sampleTracks[nextTrackIndex];
        nextTrackIndex = (nextTrackIndex + 1) % sampleTracks.size();
    }

    // Calculate final volume
    float finalVolume = (volume / 100.0f) * (sampleVolume / 100.0f);
    MIX_SetTrackGain(sampleTrack, finalVolume);
    MIX_SetTrackAudio(sampleTrack, it->second);
    MIX_PlayTrack(sampleTrack, 0);
}

void AudioManager::clearSamples() {
    for (auto* t : sampleTracks) {
        if (t) {
            MIX_StopTrack(t, 0);
            MIX_DestroyTrack(t);
        }
    }
    sampleTracks.clear();

    for (auto& pair : sampleCache) {
        if (pair.second) {
            MIX_DestroyAudio(pair.second);
        }
    }
    sampleCache.clear();
    nextSampleHandle = 1;
}

void AudioManager::setSampleVolume(int volume) {
    sampleVolume = std::max(0, std::min(100, volume));
}

int AudioManager::getSampleVolume() const {
    return sampleVolume;
}
