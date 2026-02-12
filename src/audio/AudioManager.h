#pragma once
#define NOMINMAX
#include <string>
#include <vector>
#include <unordered_map>
#include <bass.h>
#include <bass_fx.h>

#ifdef _WIN32
#include <windows.h>
#include <basswasapi.h>
#include <bassasio.h>
#include <bassmix.h>
#endif

enum class AudioOutputMode {
    DirectSound = 0,
    WasapiShared = 1,
    WasapiExclusive = 2,
    ASIO = 3
};

#ifdef _WIN32
// Runtime-loaded function pointers for BASSmix
struct MixerFuncs {
    decltype(&BASS_Mixer_StreamCreate) StreamCreate = nullptr;
    decltype(&BASS_Mixer_StreamAddChannel) StreamAddChannel = nullptr;
    decltype(&BASS_Mixer_StreamAddChannelEx) StreamAddChannelEx = nullptr;
    decltype(&BASS_Mixer_ChannelGetMixer) ChannelGetMixer = nullptr;
    decltype(&BASS_Mixer_ChannelFlags) ChannelFlags = nullptr;
    decltype(&BASS_Mixer_ChannelRemove) ChannelRemove = nullptr;
    decltype(&BASS_Mixer_ChannelSetPosition) ChannelSetPosition = nullptr;
    decltype(&BASS_Mixer_ChannelGetPosition) ChannelGetPosition = nullptr;
    decltype(&BASS_Mixer_ChannelGetPositionEx) ChannelGetPositionEx = nullptr;
    decltype(&BASS_Mixer_ChannelIsActive) ChannelIsActive = nullptr;
    bool loaded = false;
};

// Runtime-loaded function pointers for BASSWASAPI
struct WasapiFuncs {
    decltype(&BASS_WASAPI_Init) Init = nullptr;
    decltype(&BASS_WASAPI_Free) Free = nullptr;
    decltype(&BASS_WASAPI_Start) Start = nullptr;
    decltype(&BASS_WASAPI_Stop) Stop = nullptr;
    decltype(&BASS_WASAPI_IsStarted) IsStarted = nullptr;
    decltype(&BASS_WASAPI_GetDeviceInfo) GetDeviceInfo = nullptr;
    decltype(&BASS_WASAPI_GetInfo) GetInfo = nullptr;
    decltype(&BASS_WASAPI_SetVolume) SetVolume = nullptr;
    decltype(&BASS_WASAPI_GetVolume) GetVolume = nullptr;
    decltype(&BASS_WASAPI_CheckFormat) CheckFormat = nullptr;
    bool loaded = false;
};

// Runtime-loaded function pointers for BASSASIO
struct AsioFuncs {
    decltype(&BASS_ASIO_Init) Init = nullptr;
    decltype(&BASS_ASIO_Free) Free = nullptr;
    decltype(&BASS_ASIO_Start) Start = nullptr;
    decltype(&BASS_ASIO_Stop) Stop = nullptr;
    decltype(&BASS_ASIO_IsStarted) IsStarted = nullptr;
    decltype(&BASS_ASIO_GetDeviceInfo) GetDeviceInfo = nullptr;
    decltype(&BASS_ASIO_GetInfo) GetInfo = nullptr;
    decltype(&BASS_ASIO_SetRate) SetRate = nullptr;
    decltype(&BASS_ASIO_GetRate) GetRate = nullptr;
    decltype(&BASS_ASIO_ChannelEnable) ChannelEnable = nullptr;
    decltype(&BASS_ASIO_ChannelEnableBASS) ChannelEnableBASS = nullptr;
    decltype(&BASS_ASIO_ChannelJoin) ChannelJoin = nullptr;
    decltype(&BASS_ASIO_ChannelSetFormat) ChannelSetFormat = nullptr;
    decltype(&BASS_ASIO_ChannelSetRate) ChannelSetRate = nullptr;
    decltype(&BASS_ASIO_ControlPanel) ControlPanel = nullptr;
    decltype(&BASS_ASIO_ErrorGetCode) ErrorGetCode = nullptr;
    bool loaded = false;
};
#endif

class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    // Initialize with output mode and settings
    bool init(int outputMode = 0, int device = -1, int bufferMs = 40, int asioDevice = 0);
    void shutdown();
    bool reinitialize(int outputMode, int device, int bufferMs, int asioDevice = 0);

    bool loadMusic(const std::string& filepath, bool loop = true);

    void play();
    void stop();
    void fadeIn(int ms);
    void fadeOut(int ms);
    void pause();
    void resume();
    int64_t getPosition() const;
    int64_t getRawPosition() const { return getPosition(); }
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
    void playSample(int handle, int volume = 100, int64_t offsetMs = 0);
    void pauseAllSamples();
    void resumeAllSamples();
    void stopAllSamples();
    void clearSamples();
    void setSampleVolume(int volume);
    int getSampleVolume() const;
    void warmupSamples();

    // Playback speed control (for DT/HT mods)
    void setPlaybackRate(float rate);
    float getPlaybackRate() const;
    void setChangePitch(bool change);
    bool getChangePitch() const;

    // Output mode info
    AudioOutputMode getOutputMode() const { return outputMode; }
    bool isUsingMixer() const { return useMixer; }

#ifdef _WIN32
    // Addon availability
    bool isWasapiAvailable() const { return wasapiFuncs.loaded; }
    bool isAsioAvailable() const { return asioFuncs.loaded; }
    bool isMixerAvailable() const { return mixerFuncs.loaded; }

    // WASAPI/ASIO device enumeration
    std::vector<std::string> getWasapiDevices();
    std::vector<std::string> getAsioDevices();
    void openAsioControlPanel();
#else
    bool isWasapiAvailable() const { return false; }
    bool isAsioAvailable() const { return false; }
    bool isMixerAvailable() const { return false; }
#endif

private:
    HSTREAM decodeStream;
    HSTREAM tempoStream;
    HSTREAM mixerStream;    // BASSmix mixer stream
    bool initialized;
    bool useMixer;          // true for WASAPI/ASIO modes
    static volatile bool shuttingDown;  // signal callback to return silence
    AudioOutputMode outputMode;
    int bufferSizeMs;
    int currentVolume;
    int sampleVolume;
    float playbackRate;
    bool changePitch;
    bool loopMusic;

    // Sample cache: handle -> HSAMPLE
    std::unordered_map<int, HSAMPLE> sampleCache;
    int nextSampleHandle;

    // Track mixer source channels for cleanup
    std::vector<DWORD> activeMixerChannels;
    void cleanupMixerChannels();

#ifdef _WIN32
    // Runtime DLL handles
    HMODULE hMixerDll = nullptr;
    HMODULE hWasapiDll = nullptr;
    HMODULE hAsioDll = nullptr;

    // Function pointer structs
    MixerFuncs mixerFuncs;
    WasapiFuncs wasapiFuncs;
    AsioFuncs asioFuncs;

    void loadAddonDLLs();
    void unloadAddonDLLs();

    bool initDirectSound(int device, int bufferMs);
    bool initWasapiShared(int device, int bufferMs);
    bool initWasapiExclusive(int device, int bufferMs);
    bool initAsio(int device);

    // WASAPI callback
    static DWORD CALLBACK WasapiProc(void* buffer, DWORD length, void* user);
    // ASIO callback
    static DWORD CALLBACK AsioProc(BOOL input, DWORD channel, void* buffer, DWORD length, void* user);
#else
    bool initDirectSound(int device, int bufferMs);
#endif

#ifndef _WIN32
    HSAMPLE loadSampleWithFFmpeg(const void* data, size_t size);
#endif
};
