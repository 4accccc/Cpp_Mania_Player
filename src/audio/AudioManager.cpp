#include "AudioManager.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <SDL3/SDL.h>

// Perceptual volume curve: maps linear 0-100 to exponential 0.0-1.0
// Human hearing is logarithmic, so a quadratic curve feels more natural
static float perceptualVolume(int vol) {
    float v = (std::max)(0, (std::min)(100, vol)) / 100.0f;
    return v * v;
}

#ifdef _WIN32
#include <windows.h>
// Convert ANSI (system codepage) string to UTF-8
static std::string ansiToUtf8(const char* ansi) {
    if (!ansi || !ansi[0]) return "";
    int wlen = MultiByteToWideChar(CP_ACP, 0, ansi, -1, nullptr, 0);
    if (wlen <= 0) return ansi;
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_ACP, 0, ansi, -1, &wstr[0], wlen);
    int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (ulen <= 0) return ansi;
    std::string utf8(ulen, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8[0], ulen, nullptr, nullptr);
    if (!utf8.empty() && utf8.back() == '\0') utf8.pop_back();
    return utf8;
}
#endif

#ifndef _WIN32
#include <fstream>
#include <filesystem>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}
namespace fs = std::filesystem;
#endif

// ============================================================
// Runtime DLL loading (Windows only)
// ============================================================
#ifdef _WIN32
#define LOAD_FUNC(dll, name) reinterpret_cast<decltype(&name)>(GetProcAddress(dll, #name))

void AudioManager::loadAddonDLLs() {
    // BASSmix
    hMixerDll = LoadLibraryA("bassmix.dll");
    if (hMixerDll) {
        mixerFuncs.StreamCreate = LOAD_FUNC(hMixerDll, BASS_Mixer_StreamCreate);
        mixerFuncs.StreamAddChannel = LOAD_FUNC(hMixerDll, BASS_Mixer_StreamAddChannel);
        mixerFuncs.StreamAddChannelEx = LOAD_FUNC(hMixerDll, BASS_Mixer_StreamAddChannelEx);
        mixerFuncs.ChannelGetMixer = LOAD_FUNC(hMixerDll, BASS_Mixer_ChannelGetMixer);
        mixerFuncs.ChannelFlags = LOAD_FUNC(hMixerDll, BASS_Mixer_ChannelFlags);
        mixerFuncs.ChannelRemove = LOAD_FUNC(hMixerDll, BASS_Mixer_ChannelRemove);
        mixerFuncs.ChannelSetPosition = LOAD_FUNC(hMixerDll, BASS_Mixer_ChannelSetPosition);
        mixerFuncs.ChannelGetPosition = LOAD_FUNC(hMixerDll, BASS_Mixer_ChannelGetPosition);
        mixerFuncs.ChannelGetPositionEx = LOAD_FUNC(hMixerDll, BASS_Mixer_ChannelGetPositionEx);
        mixerFuncs.ChannelIsActive = LOAD_FUNC(hMixerDll, BASS_Mixer_ChannelIsActive);
        mixerFuncs.loaded = mixerFuncs.StreamCreate && mixerFuncs.StreamAddChannel &&
                            mixerFuncs.ChannelFlags && mixerFuncs.ChannelGetPosition;
        if (mixerFuncs.loaded)
            std::cout << "BASSmix loaded" << std::endl;
        else
            std::cerr << "BASSmix: some functions missing" << std::endl;
    }

    // BASSWASAPI
    hWasapiDll = LoadLibraryA("basswasapi.dll");
    if (hWasapiDll) {
        wasapiFuncs.Init = LOAD_FUNC(hWasapiDll, BASS_WASAPI_Init);
        wasapiFuncs.Free = LOAD_FUNC(hWasapiDll, BASS_WASAPI_Free);
        wasapiFuncs.Start = LOAD_FUNC(hWasapiDll, BASS_WASAPI_Start);
        wasapiFuncs.Stop = LOAD_FUNC(hWasapiDll, BASS_WASAPI_Stop);
        wasapiFuncs.IsStarted = LOAD_FUNC(hWasapiDll, BASS_WASAPI_IsStarted);
        wasapiFuncs.GetDeviceInfo = LOAD_FUNC(hWasapiDll, BASS_WASAPI_GetDeviceInfo);
        wasapiFuncs.GetInfo = LOAD_FUNC(hWasapiDll, BASS_WASAPI_GetInfo);
        wasapiFuncs.SetVolume = LOAD_FUNC(hWasapiDll, BASS_WASAPI_SetVolume);
        wasapiFuncs.GetVolume = LOAD_FUNC(hWasapiDll, BASS_WASAPI_GetVolume);
        wasapiFuncs.CheckFormat = LOAD_FUNC(hWasapiDll, BASS_WASAPI_CheckFormat);
        wasapiFuncs.loaded = wasapiFuncs.Init && wasapiFuncs.Free &&
                             wasapiFuncs.Start && wasapiFuncs.Stop;
        if (wasapiFuncs.loaded)
            std::cout << "BASSWASAPI loaded" << std::endl;
        else
            std::cerr << "BASSWASAPI: some functions missing" << std::endl;
    }

    // BASSASIO
    hAsioDll = LoadLibraryA("bassasio.dll");
    if (hAsioDll) {
        asioFuncs.Init = LOAD_FUNC(hAsioDll, BASS_ASIO_Init);
        asioFuncs.Free = LOAD_FUNC(hAsioDll, BASS_ASIO_Free);
        asioFuncs.Start = LOAD_FUNC(hAsioDll, BASS_ASIO_Start);
        asioFuncs.Stop = LOAD_FUNC(hAsioDll, BASS_ASIO_Stop);
        asioFuncs.IsStarted = LOAD_FUNC(hAsioDll, BASS_ASIO_IsStarted);
        asioFuncs.GetDeviceInfo = LOAD_FUNC(hAsioDll, BASS_ASIO_GetDeviceInfo);
        asioFuncs.GetInfo = LOAD_FUNC(hAsioDll, BASS_ASIO_GetInfo);
        asioFuncs.SetRate = LOAD_FUNC(hAsioDll, BASS_ASIO_SetRate);
        asioFuncs.GetRate = LOAD_FUNC(hAsioDll, BASS_ASIO_GetRate);
        asioFuncs.ChannelEnable = LOAD_FUNC(hAsioDll, BASS_ASIO_ChannelEnable);
        asioFuncs.ChannelEnableBASS = LOAD_FUNC(hAsioDll, BASS_ASIO_ChannelEnableBASS);
        asioFuncs.ChannelJoin = LOAD_FUNC(hAsioDll, BASS_ASIO_ChannelJoin);
        asioFuncs.ChannelSetFormat = LOAD_FUNC(hAsioDll, BASS_ASIO_ChannelSetFormat);
        asioFuncs.ChannelSetRate = LOAD_FUNC(hAsioDll, BASS_ASIO_ChannelSetRate);
        asioFuncs.ControlPanel = LOAD_FUNC(hAsioDll, BASS_ASIO_ControlPanel);
        asioFuncs.ErrorGetCode = LOAD_FUNC(hAsioDll, BASS_ASIO_ErrorGetCode);
        asioFuncs.loaded = asioFuncs.Init && asioFuncs.Free &&
                           asioFuncs.Start && asioFuncs.ChannelEnableBASS;
        if (asioFuncs.loaded)
            std::cout << "BASSASIO loaded" << std::endl;
        else
            std::cerr << "BASSASIO: some functions missing" << std::endl;
    }
}

void AudioManager::unloadAddonDLLs() {
    if (hAsioDll) { FreeLibrary(hAsioDll); hAsioDll = nullptr; }
    if (hWasapiDll) { FreeLibrary(hWasapiDll); hWasapiDll = nullptr; }
    if (hMixerDll) { FreeLibrary(hMixerDll); hMixerDll = nullptr; }
    asioFuncs = {};
    wasapiFuncs = {};
    mixerFuncs = {};
}

// WASAPI callback: read from mixer stream
DWORD CALLBACK AudioManager::WasapiProc(void* buffer, DWORD length, void* user) {
    if (shuttingDown) {
        memset(buffer, 0, length);
        return length;
    }
    HSTREAM mixer = (HSTREAM)(intptr_t)user;
    int got = BASS_ChannelGetData(mixer, buffer, length);
    if (got < 0) {
        memset(buffer, 0, length);
        return length;
    }
    return (DWORD)got;
}

// ASIO callback: read from mixer stream
DWORD CALLBACK AudioManager::AsioProc(BOOL input, DWORD channel, void* buffer, DWORD length, void* user) {
    if (shuttingDown) {
        memset(buffer, 0, length);
        return length;
    }
    HSTREAM mixer = (HSTREAM)(intptr_t)user;
    int got = BASS_ChannelGetData(mixer, buffer, length);
    if (got <= 0) {
        memset(buffer, 0, length);
        return length;
    }
    return (DWORD)got;
}
#endif

// Static member definition
volatile bool AudioManager::shuttingDown = false;

// ============================================================
// Constructor / Destructor
// ============================================================
AudioManager::AudioManager()
    : decodeStream(0), tempoStream(0), mixerStream(0),
      initialized(false), useMixer(false),
      outputMode(AudioOutputMode::DirectSound), bufferSizeMs(40),
      currentVolume(100), sampleVolume(100), playbackRate(1.0f),
      changePitch(false), loopMusic(false), nextSampleHandle(1) {}

AudioManager::~AudioManager() {
    shutdown();
}

void AudioManager::shutdown() {
    // 1. Signal callback to return silence immediately
    shuttingDown = true;
    SDL_Delay(10);  // give callback thread time to see the flag

    // 2. Stop output device (now safe - callback returns silence)
#ifdef _WIN32
    if (outputMode == AudioOutputMode::ASIO && asioFuncs.loaded) {
        if (asioFuncs.IsStarted && asioFuncs.IsStarted()) asioFuncs.Stop();
        if (asioFuncs.Free) asioFuncs.Free();
    }
    if ((outputMode == AudioOutputMode::WasapiShared || outputMode == AudioOutputMode::WasapiExclusive)
        && wasapiFuncs.loaded) {
        if (wasapiFuncs.IsStarted && wasapiFuncs.IsStarted()) wasapiFuncs.Stop(TRUE);
        if (wasapiFuncs.Free) wasapiFuncs.Free();
    }
#endif

    // 3. Free streams and samples
    clearSamples();
    cleanupMixerChannels();
    if (tempoStream) { BASS_StreamFree(tempoStream); tempoStream = 0; decodeStream = 0; }
    if (mixerStream) { BASS_StreamFree(mixerStream); mixerStream = 0; }
    if (initialized) { BASS_Free(); initialized = false; }

#ifdef _WIN32
    unloadAddonDLLs();
#endif
    useMixer = false;
}

void AudioManager::cleanupMixerChannels() {
#ifdef _WIN32
    if (mixerFuncs.loaded && mixerFuncs.ChannelGetMixer) {
        // Remove channels no longer in the mixer (AUTOFREE'd after playback ended)
        activeMixerChannels.erase(
            std::remove_if(activeMixerChannels.begin(), activeMixerChannels.end(),
                [this](DWORD ch) { return !mixerFuncs.ChannelGetMixer(ch); }),
            activeMixerChannels.end());
        return;
    }
#endif
    activeMixerChannels.clear();
}

// ============================================================
// Initialization
// ============================================================
bool AudioManager::init(int mode, int device, int bufferMs, int asioDeviceIdx) {
    shuttingDown = false;
#ifdef _WIN32
    loadAddonDLLs();

    outputMode = static_cast<AudioOutputMode>(mode);
    bufferSizeMs = bufferMs;

    switch (outputMode) {
        case AudioOutputMode::WasapiShared:
            if (wasapiFuncs.loaded && mixerFuncs.loaded) {
                if (initWasapiShared(device, bufferMs)) return true;
                std::cerr << "WASAPI Shared init failed, falling back to DirectSound" << std::endl;
            }
            outputMode = AudioOutputMode::DirectSound;
            return initDirectSound(device, bufferMs);

        case AudioOutputMode::WasapiExclusive:
            if (wasapiFuncs.loaded && mixerFuncs.loaded) {
                if (initWasapiExclusive(device, bufferMs)) return true;
                std::cerr << "WASAPI Exclusive init failed, falling back to DirectSound" << std::endl;
            }
            outputMode = AudioOutputMode::DirectSound;
            return initDirectSound(device, bufferMs);

        case AudioOutputMode::ASIO:
            if (asioFuncs.loaded && mixerFuncs.loaded) {
                if (initAsio(asioDeviceIdx)) return true;
                std::cerr << "ASIO init failed, falling back to DirectSound" << std::endl;
            }
            outputMode = AudioOutputMode::DirectSound;
            return initDirectSound(device, bufferMs);

        default:
            outputMode = AudioOutputMode::DirectSound;
            return initDirectSound(device, bufferMs);
    }
#else
    outputMode = AudioOutputMode::DirectSound;
    bufferSizeMs = bufferMs;
    return initDirectSound(device, bufferMs);
#endif
}

bool AudioManager::reinitialize(int mode, int device, int bufferMs, int asioDeviceIdx) {
    // Lightweight shutdown: don't unload DLLs, just stop and free audio resources
    shuttingDown = true;
    SDL_Delay(20);

#ifdef _WIN32
    if (outputMode == AudioOutputMode::ASIO && asioFuncs.loaded) {
        if (asioFuncs.IsStarted && asioFuncs.IsStarted()) asioFuncs.Stop();
        if (asioFuncs.Free) asioFuncs.Free();
    }
    if ((outputMode == AudioOutputMode::WasapiShared || outputMode == AudioOutputMode::WasapiExclusive)
        && wasapiFuncs.loaded) {
        if (wasapiFuncs.IsStarted && wasapiFuncs.IsStarted()) wasapiFuncs.Stop(FALSE);
        if (wasapiFuncs.Free) wasapiFuncs.Free();
    }
#endif

    clearSamples();
    cleanupMixerChannels();
    if (tempoStream) { BASS_StreamFree(tempoStream); tempoStream = 0; decodeStream = 0; }
    if (mixerStream) { BASS_StreamFree(mixerStream); mixerStream = 0; }
    if (initialized) { BASS_Free(); initialized = false; }
    useMixer = false;

    SDL_Delay(50);  // let OS release audio device

    // Re-init without reloading DLLs
    shuttingDown = false;
    outputMode = static_cast<AudioOutputMode>(mode);
    bufferSizeMs = bufferMs;

#ifdef _WIN32
    switch (outputMode) {
        case AudioOutputMode::WasapiShared:
            if (wasapiFuncs.loaded && mixerFuncs.loaded) {
                if (initWasapiShared(device, bufferMs)) return true;
            }
            outputMode = AudioOutputMode::DirectSound;
            return initDirectSound(device, bufferMs);
        case AudioOutputMode::WasapiExclusive:
            if (wasapiFuncs.loaded && mixerFuncs.loaded) {
                if (initWasapiExclusive(device, bufferMs)) return true;
            }
            outputMode = AudioOutputMode::DirectSound;
            return initDirectSound(device, bufferMs);
        case AudioOutputMode::ASIO:
            if (asioFuncs.loaded && mixerFuncs.loaded) {
                if (initAsio(asioDeviceIdx)) return true;
            }
            outputMode = AudioOutputMode::DirectSound;
            return initDirectSound(device, bufferMs);
        default:
            return initDirectSound(device, bufferMs);
    }
#else
    return initDirectSound(device, bufferMs);
#endif
}

#ifdef _WIN32
bool AudioManager::initDirectSound(int device, int bufferMs) {
    // Optimize buffer settings before init
    BASS_SetConfig(BASS_CONFIG_UPDATEPERIOD, 5);
    BASS_SetConfig(BASS_CONFIG_DEV_BUFFER, bufferMs);

    int bassDevice = (device <= 0) ? -1 : device;
    if (!BASS_Init(bassDevice, 44100, 0, 0, nullptr)) {
        std::cerr << "BASS_Init (DirectSound) failed: " << BASS_ErrorGetCode() << std::endl;
        return false;
    }
    initialized = true;
    useMixer = false;
    std::cout << "Audio: DirectSound, buffer=" << bufferMs << "ms" << std::endl;
    return true;
}

bool AudioManager::initWasapiShared(int device, int bufferMs) {
    int wasapiDevice = (device <= 0) ? -1 : device;

    // Query device mix format (shared mode must match system mixer rate)
    DWORD mixFreq = 48000, mixChans = 2;
    if (wasapiFuncs.GetDeviceInfo) {
        BASS_WASAPI_DEVICEINFO devInfo = {};
        int queryDev = (wasapiDevice == -1) ? -1 : wasapiDevice;
        if (wasapiFuncs.GetDeviceInfo(queryDev, &devInfo)) {
            if (devInfo.mixfreq > 0) mixFreq = devInfo.mixfreq;
            if (devInfo.mixchans > 0) mixChans = devInfo.mixchans;
        }
    }

    // BASS "no sound" device for decoding (match device sample rate)
    if (!BASS_Init(0, mixFreq, 0, 0, nullptr)) {
        std::cerr << "BASS_Init(0) failed: " << BASS_ErrorGetCode() << std::endl;
        return false;
    }
    initialized = true;

    // Create mixer at device's native sample rate and channel count
    mixerStream = mixerFuncs.StreamCreate(mixFreq, mixChans,
        BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_MIXER_NONSTOP);
    if (!mixerStream) {
        std::cerr << "Mixer create failed: " << BASS_ErrorGetCode() << std::endl;
        BASS_Free(); initialized = false;
        return false;
    }

    // Init WASAPI shared mode (freq=0, chans=0 = use device default)
    float bufSec = bufferMs / 1000.0f;
    if (!wasapiFuncs.Init(wasapiDevice, 0, 0, BASS_WASAPI_EVENT,
                          bufSec, 0, &WasapiProc, (void*)(intptr_t)mixerStream)) {
        std::cerr << "WASAPI Shared init failed: " << BASS_ErrorGetCode() << std::endl;
        BASS_StreamFree(mixerStream); mixerStream = 0;
        BASS_Free(); initialized = false;
        return false;
    }

    if (!wasapiFuncs.Start()) {
        std::cerr << "WASAPI Start failed: " << BASS_ErrorGetCode() << std::endl;
        wasapiFuncs.Free();
        BASS_StreamFree(mixerStream); mixerStream = 0;
        BASS_Free(); initialized = false;
        return false;
    }

    useMixer = true;
    std::cout << "Audio: WASAPI Shared, " << mixFreq << "Hz " << mixChans << "ch, buffer=" << bufferMs << "ms" << std::endl;
    return true;
}

bool AudioManager::initWasapiExclusive(int device, int bufferMs) {
    int wasapiDevice = (device <= 0) ? -1 : device;

    // Query device preferred format for initial attempt
    DWORD initFreq = 44100, initChans = 2;
    if (wasapiFuncs.GetDeviceInfo) {
        BASS_WASAPI_DEVICEINFO devInfo = {};
        if (wasapiFuncs.GetDeviceInfo(wasapiDevice, &devInfo)) {
            if (devInfo.mixfreq > 0) initFreq = devInfo.mixfreq;
            if (devInfo.mixchans > 0) initChans = devInfo.mixchans;
        }
    }

    if (!BASS_Init(0, initFreq, 0, 0, nullptr)) {
        std::cerr << "BASS_Init(0) failed: " << BASS_ErrorGetCode() << std::endl;
        return false;
    }
    initialized = true;

    mixerStream = mixerFuncs.StreamCreate(initFreq, initChans,
        BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_MIXER_NONSTOP);
    if (!mixerStream) {
        std::cerr << "Mixer create failed: " << BASS_ErrorGetCode() << std::endl;
        BASS_Free(); initialized = false;
        return false;
    }

    float bufSec = bufferMs / 1000.0f;
    if (!wasapiFuncs.Init(wasapiDevice, initFreq, initChans,
                          BASS_WASAPI_EXCLUSIVE | BASS_WASAPI_EVENT | BASS_WASAPI_AUTOFORMAT,
                          bufSec, 0, &WasapiProc, (void*)(intptr_t)mixerStream)) {
        std::cerr << "WASAPI Exclusive init failed: " << BASS_ErrorGetCode() << std::endl;
        BASS_StreamFree(mixerStream); mixerStream = 0;
        BASS_Free(); initialized = false;
        return false;
    }

    // Check if AUTOFORMAT negotiated a different rate; rebuild mixer if needed
    DWORD actualFreq = initFreq, actualChans = initChans;
    if (wasapiFuncs.GetInfo) {
        BASS_WASAPI_INFO wInfo = {};
        if (wasapiFuncs.GetInfo(&wInfo)) {
            actualFreq = wInfo.freq;
            actualChans = wInfo.chans;
        }
    }
    if (actualFreq != initFreq || actualChans != initChans) {
        wasapiFuncs.Stop(TRUE);
        wasapiFuncs.Free();
        BASS_StreamFree(mixerStream);

        mixerStream = mixerFuncs.StreamCreate(actualFreq, actualChans,
            BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_MIXER_NONSTOP);
        if (!mixerStream) {
            BASS_Free(); initialized = false;
            return false;
        }

        if (!wasapiFuncs.Init(wasapiDevice, actualFreq, actualChans,
                              BASS_WASAPI_EXCLUSIVE | BASS_WASAPI_EVENT,
                              bufSec, 0, &WasapiProc, (void*)(intptr_t)mixerStream)) {
            BASS_StreamFree(mixerStream); mixerStream = 0;
            BASS_Free(); initialized = false;
            return false;
        }
    }

    if (!wasapiFuncs.Start()) {
        std::cerr << "WASAPI Exclusive Start failed: " << BASS_ErrorGetCode() << std::endl;
        wasapiFuncs.Free();
        BASS_StreamFree(mixerStream); mixerStream = 0;
        BASS_Free(); initialized = false;
        return false;
    }

    useMixer = true;
    std::cout << "Audio: WASAPI Exclusive, " << actualFreq << "Hz " << actualChans << "ch, buffer=" << bufferMs << "ms" << std::endl;
    return true;
}

bool AudioManager::initAsio(int device) {
    // Init ASIO first to query native sample rate
    BASS_Init(0, 48000, 0, 0, nullptr);
    if (!asioFuncs.Init(device, BASS_ASIO_THREAD)) {
        std::cerr << "ASIO init failed: " << (asioFuncs.ErrorGetCode ? asioFuncs.ErrorGetCode() : -1) << std::endl;
        BASS_Free();
        return false;
    }
    DWORD asioFreq = 48000;
    if (asioFuncs.GetRate) {
        double rate = asioFuncs.GetRate();
        if (rate > 0) asioFreq = (DWORD)rate;
    }
    asioFuncs.Free();
    BASS_Free();

    // Real init at the correct rate
    if (!BASS_Init(0, asioFreq, 0, 0, nullptr)) {
        std::cerr << "BASS_Init(0) failed: " << BASS_ErrorGetCode() << std::endl;
        return false;
    }
    initialized = true;

    mixerStream = mixerFuncs.StreamCreate(asioFreq, 2,
        BASS_SAMPLE_FLOAT | BASS_STREAM_DECODE | BASS_MIXER_NONSTOP);
    if (!mixerStream) {
        std::cerr << "Mixer create failed: " << BASS_ErrorGetCode() << std::endl;
        BASS_Free(); initialized = false;
        return false;
    }

    if (!asioFuncs.Init(device, BASS_ASIO_THREAD)) {
        std::cerr << "ASIO init failed: " << (asioFuncs.ErrorGetCode ? asioFuncs.ErrorGetCode() : -1) << std::endl;
        BASS_StreamFree(mixerStream); mixerStream = 0;
        BASS_Free(); initialized = false;
        return false;
    }

    if (asioFuncs.SetRate) asioFuncs.SetRate((double)asioFreq);

    // Use ChannelEnableBASS for automatic format handling
    if (!asioFuncs.ChannelEnableBASS(FALSE, 0, mixerStream, TRUE)) {
        std::cerr << "ASIO ChannelEnableBASS failed: "
                  << (asioFuncs.ErrorGetCode ? asioFuncs.ErrorGetCode() : -1) << std::endl;
        asioFuncs.Free();
        BASS_StreamFree(mixerStream); mixerStream = 0;
        BASS_Free(); initialized = false;
        return false;
    }

    if (!asioFuncs.Start(0, 0)) {
        std::cerr << "ASIO Start failed: " << (asioFuncs.ErrorGetCode ? asioFuncs.ErrorGetCode() : -1) << std::endl;
        asioFuncs.Free();
        BASS_StreamFree(mixerStream); mixerStream = 0;
        BASS_Free(); initialized = false;
        return false;
    }

    useMixer = true;
    std::cout << "Audio: ASIO device " << device << ", " << asioFreq << "Hz" << std::endl;
    return true;
}

// Device enumeration
std::vector<std::string> AudioManager::getWasapiDevices() {
    std::vector<std::string> devices;
    if (!wasapiFuncs.loaded || !wasapiFuncs.GetDeviceInfo) return devices;
    BASS_WASAPI_DEVICEINFO info;
    for (int i = 0; wasapiFuncs.GetDeviceInfo(i, &info); i++) {
        if ((info.flags & BASS_DEVICE_ENABLED) && !(info.flags & BASS_DEVICE_INPUT)) {
            devices.push_back(ansiToUtf8(info.name));
        }
    }
    if (devices.empty()) devices.push_back("Default");
    return devices;
}

std::vector<std::string> AudioManager::getAsioDevices() {
    std::vector<std::string> devices;
    if (!asioFuncs.loaded || !asioFuncs.GetDeviceInfo) return devices;
    BASS_ASIO_DEVICEINFO info;
    for (int i = 0; asioFuncs.GetDeviceInfo(i, &info); i++) {
        devices.push_back(ansiToUtf8(info.name));
    }
    return devices;
}

void AudioManager::openAsioControlPanel() {
    if (asioFuncs.loaded && asioFuncs.ControlPanel) {
        asioFuncs.ControlPanel();
    }
}

#else
// Linux: only DirectSound (standard BASS output)
bool AudioManager::initDirectSound(int device, int bufferMs) {
    BASS_SetConfig(BASS_CONFIG_UPDATEPERIOD, 5);
    BASS_SetConfig(BASS_CONFIG_DEV_BUFFER, bufferMs);

    int bassDevice = (device <= 0) ? -1 : device;
    if (!BASS_Init(bassDevice, 44100, 0, 0, nullptr)) {
        std::cerr << "BASS_Init failed: " << BASS_ErrorGetCode() << std::endl;
        return false;
    }
    initialized = true;
    useMixer = false;
    std::cout << "Audio: DirectSound, buffer=" << bufferMs << "ms" << std::endl;
    return true;
}
#endif

// ============================================================
// Music loading and playback
// ============================================================
bool AudioManager::loadMusic(const std::string& filepath, bool loop) {
    // Clean up previous streams
    if (tempoStream) {
        // Remove from mixer first
#ifdef _WIN32
        if (useMixer && mixerFuncs.loaded && mixerFuncs.ChannelRemove) {
            mixerFuncs.ChannelRemove(tempoStream);
        }
#endif
        BASS_StreamFree(tempoStream);
        tempoStream = 0;
        decodeStream = 0;
    }

    loopMusic = loop;

    // Create decode stream
    DWORD flags = BASS_STREAM_DECODE | BASS_STREAM_PRESCAN;
    if (loop) flags |= BASS_SAMPLE_LOOP;

    decodeStream = BASS_StreamCreateFile(FALSE, filepath.c_str(), 0, 0, flags);
    if (!decodeStream) {
        std::cerr << "BASS_StreamCreateFile failed: " << BASS_ErrorGetCode() << std::endl;
        return false;
    }

    // Create tempo stream wrapping the decode stream
    // In mixer mode, tempo stream must also be DECODE so mixer can pull from it
    DWORD tempoFlags = BASS_FX_FREESOURCE;
    if (loop) tempoFlags |= BASS_SAMPLE_LOOP;
    if (useMixer) tempoFlags |= BASS_STREAM_DECODE;

    tempoStream = BASS_FX_TempoCreate(decodeStream, tempoFlags);
    if (!tempoStream) {
        std::cerr << "BASS_FX_TempoCreate failed: " << BASS_ErrorGetCode() << std::endl;
        BASS_StreamFree(decodeStream);
        decodeStream = 0;
        return false;
    }

    // Optimize SoundTouch parameters
    BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO_OPTION_USE_QUICKALGO, 0);
    BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO_OPTION_USE_AA_FILTER, 1);
    BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO_OPTION_AA_FILTER_LENGTH, 64);
    BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO_OPTION_SEQUENCE_MS, 82);
    BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO_OPTION_SEEKWINDOW_MS, 14);
    BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO_OPTION_OVERLAP_MS, 12);
    BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO_OPTION_PREVENT_CLICK, 1);

    // Add to mixer if using mixer mode
#ifdef _WIN32
    if (useMixer && mixerFuncs.loaded && mixerStream) {
        // Add tempo stream to mixer (paused initially)
        if (!mixerFuncs.StreamAddChannel(mixerStream, tempoStream,
                BASS_MIXER_CHAN_PAUSE | BASS_MIXER_CHAN_NORAMPIN)) {
            std::cerr << "Mixer add music failed: " << BASS_ErrorGetCode() << std::endl;
            BASS_StreamFree(tempoStream);
            tempoStream = 0; decodeStream = 0;
            return false;
        }
    }
#endif

    // Apply current settings
    setVolume(currentVolume);
    setPlaybackRate(playbackRate);

    std::cout << "Loaded music: " << filepath << std::endl;
    return true;
}

void AudioManager::play() {
    if (!tempoStream) return;
#ifdef _WIN32
    if (useMixer && mixerFuncs.loaded) {
        // Unpause in mixer
        mixerFuncs.ChannelFlags(tempoStream, 0, BASS_MIXER_CHAN_PAUSE);
        return;
    }
#endif
    BASS_ChannelPlay(tempoStream, FALSE);
}

void AudioManager::stop() {
    if (!tempoStream) return;
#ifdef _WIN32
    if (useMixer && mixerFuncs.loaded) {
        // Pause in mixer and reset position
        mixerFuncs.ChannelFlags(tempoStream, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
        HSTREAM source = BASS_FX_TempoGetSource(tempoStream);
        if (source) BASS_ChannelSetPosition(source, 0, BASS_POS_BYTE);
        return;
    }
#endif
    BASS_ChannelStop(tempoStream);
    BASS_ChannelSetPosition(tempoStream, 0, BASS_POS_BYTE);
}

void AudioManager::fadeIn(int ms) {
    if (!tempoStream) return;
#ifdef _WIN32
    if (useMixer && mixerFuncs.loaded) {
        BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_VOL, 0);
        mixerFuncs.ChannelFlags(tempoStream, 0, BASS_MIXER_CHAN_PAUSE);
        BASS_ChannelSlideAttribute(tempoStream, BASS_ATTRIB_VOL, perceptualVolume(currentVolume), ms);
        return;
    }
#endif
    BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_VOL, 0);
    BASS_ChannelPlay(tempoStream, FALSE);
    BASS_ChannelSlideAttribute(tempoStream, BASS_ATTRIB_VOL, perceptualVolume(currentVolume), ms);
}

void AudioManager::fadeOut(int ms) {
    if (!tempoStream) return;
    BASS_ChannelSlideAttribute(tempoStream, BASS_ATTRIB_VOL, 0, ms);
}

void AudioManager::pause() {
    if (!tempoStream) return;
#ifdef _WIN32
    if (useMixer && mixerFuncs.loaded) {
        mixerFuncs.ChannelFlags(tempoStream, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
        return;
    }
#endif
    BASS_ChannelPause(tempoStream);
}

void AudioManager::resume() {
    if (!tempoStream) return;
#ifdef _WIN32
    if (useMixer && mixerFuncs.loaded) {
        mixerFuncs.ChannelFlags(tempoStream, 0, BASS_MIXER_CHAN_PAUSE);
        return;
    }
#endif
    BASS_ChannelPlay(tempoStream, FALSE);
}

int64_t AudioManager::getPosition() const {
    if (!tempoStream) return 0;

#ifdef _WIN32
    if (useMixer && mixerFuncs.loaded && mixerFuncs.ChannelGetPosition) {
        QWORD pos = mixerFuncs.ChannelGetPosition(tempoStream, BASS_POS_BYTE | BASS_POS_MIXER_DELAY);
        if (pos == 0 || pos == (QWORD)-1) {
            pos = BASS_ChannelGetPosition(tempoStream, BASS_POS_BYTE);
        }
        double seconds = BASS_ChannelBytes2Seconds(tempoStream, pos);
        return static_cast<int64_t>(seconds * 1000.0);
    }
#endif

    QWORD pos = BASS_ChannelGetPosition(tempoStream, BASS_POS_BYTE);
    double seconds = BASS_ChannelBytes2Seconds(tempoStream, pos);
    return static_cast<int64_t>(seconds * 1000.0);
}

void AudioManager::setPosition(int64_t ms) {
    if (!tempoStream) return;

    double seconds = ms / 1000.0;
    HSTREAM source = BASS_FX_TempoGetSource(tempoStream);

#ifdef _WIN32
    if (useMixer && mixerFuncs.loaded && mixerFuncs.ChannelSetPosition) {
        if (source) {
            QWORD pos = BASS_ChannelSeconds2Bytes(source, seconds);
            mixerFuncs.ChannelSetPosition(tempoStream, pos, BASS_POS_BYTE | BASS_POS_MIXER_RESET);
        }
        return;
    }
#endif

    if (source) {
        QWORD pos = BASS_ChannelSeconds2Bytes(source, seconds);
        BASS_ChannelSetPosition(source, pos, BASS_POS_BYTE);
    } else {
        QWORD pos = BASS_ChannelSeconds2Bytes(tempoStream, seconds);
        BASS_ChannelSetPosition(tempoStream, pos, BASS_POS_BYTE);
    }
}

bool AudioManager::isPlaying() const {
    if (!tempoStream) return false;
#ifdef _WIN32
    if (useMixer && mixerFuncs.loaded && mixerFuncs.ChannelIsActive) {
        DWORD state = mixerFuncs.ChannelIsActive(tempoStream);
        return state == BASS_ACTIVE_PLAYING;
    }
#endif
    return BASS_ChannelIsActive(tempoStream) == BASS_ACTIVE_PLAYING;
}

int64_t AudioManager::getDuration() const {
    if (!tempoStream) return 0;
    QWORD len = BASS_ChannelGetLength(tempoStream, BASS_POS_BYTE);
    double seconds = BASS_ChannelBytes2Seconds(tempoStream, len);
    return static_cast<int64_t>(seconds * 1000.0);
}

void AudioManager::setVolume(int volume) {
    currentVolume = (std::max)(0, (std::min)(100, volume));
    if (tempoStream) {
        BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_VOL, perceptualVolume(currentVolume));
    }
}

int AudioManager::getVolume() const {
    return currentVolume;
}

std::vector<std::string> AudioManager::getAudioDevices() {
    std::vector<std::string> devices;
    BASS_DEVICEINFO info;
    for (int i = 1; BASS_GetDeviceInfo(i, &info); i++) {
        if (info.flags & BASS_DEVICE_ENABLED) {
#ifdef _WIN32
            devices.push_back(ansiToUtf8(info.name));
#else
            devices.push_back(info.name);
#endif
        }
    }
    if (devices.empty()) {
        devices.push_back("Default");
    }
    return devices;
}

int AudioManager::getDeviceCount() const {
    int count = 0;
    BASS_DEVICEINFO info;
    for (int i = 1; BASS_GetDeviceInfo(i, &info); i++) {
        if (info.flags & BASS_DEVICE_ENABLED) {
            count++;
        }
    }
    return count > 0 ? count : 1;
}

// ============================================================
// Sample (keysound) support
// ============================================================
int AudioManager::loadSample(const std::string& filepath) {
    if (!initialized) return -1;

    HSAMPLE sample = BASS_SampleLoad(FALSE, filepath.c_str(), 0, 0, 65535, BASS_SAMPLE_OVER_POS);
    if (!sample) {
        std::cerr << "BASS_SampleLoad failed: " << BASS_ErrorGetCode() << std::endl;
        return -1;
    }

    int handle = nextSampleHandle++;
    sampleCache[handle] = sample;
    return handle;
}

int AudioManager::loadSampleFromMemory(const void* data, size_t size) {
    if (!initialized || !data || size == 0) return -1;

    HSAMPLE sample = BASS_SampleLoad(TRUE, data, 0, static_cast<DWORD>(size), 65535, BASS_SAMPLE_OVER_POS);

#ifndef _WIN32
    if (!sample && BASS_ErrorGetCode() == BASS_ERROR_FILEFORM) {
        sample = loadSampleWithFFmpeg(data, size);
    }
#endif

    if (!sample) {
        std::cerr << "BASS_SampleLoad (memory) failed: " << BASS_ErrorGetCode() << std::endl;
        return -1;
    }

    int handle = nextSampleHandle++;
    sampleCache[handle] = sample;
    return handle;
}

void AudioManager::playSample(int handle, int volume, int64_t offsetMs) {
    auto it = sampleCache.find(handle);
    if (it == sampleCache.end()) return;

#ifdef _WIN32
    if (useMixer && mixerFuncs.loaded && mixerStream) {
        // Periodically prune dead channels (AUTOFREE'd after playback ended)
        if (activeMixerChannels.size() > 64) cleanupMixerChannels();

        // Mixer mode: get channel as decode stream, add to mixer
        HCHANNEL channel = BASS_SampleGetChannel(it->second, BASS_SAMCHAN_STREAM | BASS_STREAM_DECODE);
        if (!channel) return;

        float finalVolume = (volume / 100.0f) * (sampleVolume / 100.0f);
        BASS_ChannelSetAttribute(channel, BASS_ATTRIB_VOL, finalVolume);

        if (playbackRate != 1.0f) {
            float origFreq;
            BASS_ChannelGetAttribute(channel, BASS_ATTRIB_FREQ, &origFreq);
            BASS_ChannelSetAttribute(channel, BASS_ATTRIB_FREQ, origFreq * playbackRate);
        }

        if (offsetMs > 0) {
            QWORD pos = BASS_ChannelSeconds2Bytes(channel, offsetMs / 1000.0);
            BASS_ChannelSetPosition(channel, pos, BASS_POS_BYTE);
        }

        // BASS_STREAM_AUTOFREE: mixer removes channel when it ends
        mixerFuncs.StreamAddChannel(mixerStream, channel,
            BASS_STREAM_AUTOFREE | BASS_MIXER_CHAN_NORAMPIN);
        activeMixerChannels.push_back(channel);
        return;
    }
#endif

    // DirectSound mode: standard playback
    HCHANNEL channel = BASS_SampleGetChannel(it->second, BASS_SAMCHAN_NEW);
    if (!channel) return;

    float finalVolume = (volume / 100.0f) * (sampleVolume / 100.0f);
    BASS_ChannelSetAttribute(channel, BASS_ATTRIB_VOL, finalVolume);

    if (playbackRate != 1.0f) {
        float origFreq;
        BASS_ChannelGetAttribute(channel, BASS_ATTRIB_FREQ, &origFreq);
        BASS_ChannelSetAttribute(channel, BASS_ATTRIB_FREQ, origFreq * playbackRate);
    }

    if (offsetMs > 0) {
        QWORD pos = BASS_ChannelSeconds2Bytes(channel, offsetMs / 1000.0);
        BASS_ChannelSetPosition(channel, pos, BASS_POS_BYTE);
    }

    BASS_ChannelPlay(channel, FALSE);
}

void AudioManager::pauseAllSamples() {
#ifdef _WIN32
    if (useMixer && mixerFuncs.loaded) {
        // Mixer mode: BASS_SampleGetChannels can't find BASS_SAMCHAN_STREAM channels,
        // so use our tracked list instead
        for (DWORD ch : activeMixerChannels) {
            if (mixerFuncs.ChannelGetMixer && mixerFuncs.ChannelGetMixer(ch)) {
                mixerFuncs.ChannelFlags(ch, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
            }
        }
        return;
    }
#endif
    for (auto& pair : sampleCache) {
        DWORD count = BASS_SampleGetChannels(pair.second, nullptr);
        if (count > 0) {
            std::vector<HCHANNEL> channels(count);
            BASS_SampleGetChannels(pair.second, channels.data());
            for (HCHANNEL ch : channels) {
                if (BASS_ChannelIsActive(ch) == BASS_ACTIVE_PLAYING) {
                    BASS_ChannelPause(ch);
                }
            }
        }
    }
}

void AudioManager::resumeAllSamples() {
#ifdef _WIN32
    if (useMixer && mixerFuncs.loaded) {
        for (DWORD ch : activeMixerChannels) {
            if (mixerFuncs.ChannelGetMixer && mixerFuncs.ChannelGetMixer(ch)) {
                mixerFuncs.ChannelFlags(ch, 0, BASS_MIXER_CHAN_PAUSE);
            }
        }
        return;
    }
#endif
    for (auto& pair : sampleCache) {
        DWORD count = BASS_SampleGetChannels(pair.second, nullptr);
        if (count > 0) {
            std::vector<HCHANNEL> channels(count);
            BASS_SampleGetChannels(pair.second, channels.data());
            for (HCHANNEL ch : channels) {
                if (BASS_ChannelIsActive(ch) == BASS_ACTIVE_PAUSED) {
                    BASS_ChannelPlay(ch, FALSE);
                }
            }
        }
    }
}

void AudioManager::stopAllSamples() {
#ifdef _WIN32
    if (useMixer && mixerFuncs.loaded && mixerFuncs.ChannelRemove) {
        for (DWORD ch : activeMixerChannels) {
            if (mixerFuncs.ChannelGetMixer && mixerFuncs.ChannelGetMixer(ch)) {
                mixerFuncs.ChannelRemove(ch);
            }
        }
        activeMixerChannels.clear();
        return;
    }
#endif
    for (auto& pair : sampleCache) {
        BASS_SampleStop(pair.second);
    }
}

void AudioManager::warmupSamples() {
    if (useMixer) {
        // In mixer mode, just briefly add/remove decode channels to warm up
        std::vector<HCHANNEL> channels;
        for (auto& pair : sampleCache) {
#ifdef _WIN32
            if (mixerFuncs.loaded && mixerStream) {
                HCHANNEL ch = BASS_SampleGetChannel(pair.second, BASS_SAMCHAN_STREAM | BASS_STREAM_DECODE);
                if (ch) {
                    BASS_ChannelSetAttribute(ch, BASS_ATTRIB_VOL, 0.0f);
                    mixerFuncs.StreamAddChannel(mixerStream, ch,
                        BASS_STREAM_AUTOFREE | BASS_MIXER_CHAN_NORAMPIN);
                    channels.push_back(ch);
                }
            }
#endif
        }
        if (!channels.empty()) {
            SDL_Delay(50);
        }
        // Channels auto-free when done, but stop them early
        for (HCHANNEL ch : channels) {
            BASS_ChannelStop(ch);
        }
        return;
    }

    // DirectSound warmup
    std::vector<HCHANNEL> channels;
    for (auto& pair : sampleCache) {
        HCHANNEL channel = BASS_SampleGetChannel(pair.second, BASS_SAMCHAN_NEW);
        if (channel) {
            BASS_ChannelSetAttribute(channel, BASS_ATTRIB_VOL, 0.0f);
            BASS_ChannelPlay(channel, FALSE);
            channels.push_back(channel);
        }
    }
    if (!channels.empty()) {
        SDL_Delay(50);
    }
    for (HCHANNEL channel : channels) {
        BASS_ChannelStop(channel);
    }
}

void AudioManager::clearSamples() {
    activeMixerChannels.clear();
    for (auto& pair : sampleCache) {
        BASS_SampleFree(pair.second);
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

// ============================================================
// Playback rate / pitch
// ============================================================
void AudioManager::setPlaybackRate(float rate) {
    playbackRate = rate;
    if (tempoStream) {
        float tempo = (rate - 1.0f) * 100.0f;
        BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO, tempo);

        if (changePitch) {
            float semitones = 12.0f * log2f(rate);
            BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO_PITCH, semitones);
        } else {
            BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO_PITCH, 0);
        }
    }
}

float AudioManager::getPlaybackRate() const {
    return playbackRate;
}

void AudioManager::setChangePitch(bool change) {
    changePitch = change;
    if (tempoStream) {
        setPlaybackRate(playbackRate);
    }
}

bool AudioManager::getChangePitch() const {
    return changePitch;
}

// ============================================================
// Linux FFmpeg transcoding
// ============================================================
#ifndef _WIN32
HSAMPLE AudioManager::loadSampleWithFFmpeg(const void* data, size_t size) {
    fs::path tempDir = fs::current_path() / "Data" / "Tmp" / "ffmpeg";
    fs::create_directories(tempDir);

    static int tempCounter = 0;
    std::string inputPath = (tempDir / ("in_" + std::to_string(tempCounter++) + ".wav")).string();

    {
        std::ofstream out(inputPath, std::ios::binary);
        if (!out) return 0;
        out.write(static_cast<const char*>(data), size);
    }

    AVFormatContext* fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, inputPath.c_str(), nullptr, nullptr) < 0) {
        fs::remove(inputPath);
        return 0;
    }

    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        avformat_close_input(&fmtCtx);
        fs::remove(inputPath);
        return 0;
    }

    int audioIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioIdx = i;
            break;
        }
    }

    if (audioIdx < 0) {
        avformat_close_input(&fmtCtx);
        fs::remove(inputPath);
        return 0;
    }

    AVCodecParameters* codecPar = fmtCtx->streams[audioIdx]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        avformat_close_input(&fmtCtx);
        fs::remove(inputPath);
        return 0;
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx, codecPar);
    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        fs::remove(inputPath);
        return 0;
    }

    SwrContext* swr = swr_alloc();
    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    AVChannelLayout inLayout;
    av_channel_layout_copy(&inLayout, &codecCtx->ch_layout);

    swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_S16, 44100,
                        &inLayout, codecCtx->sample_fmt, codecCtx->sample_rate, 0, nullptr);
    swr_init(swr);

    std::vector<uint8_t> pcmData;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    while (av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index == audioIdx) {
            if (avcodec_send_packet(codecCtx, pkt) >= 0) {
                while (avcodec_receive_frame(codecCtx, frame) >= 0) {
                    int outSamples = swr_get_out_samples(swr, frame->nb_samples);
                    std::vector<uint8_t> outBuf(outSamples * 4);
                    uint8_t* outPtr = outBuf.data();
                    int converted = swr_convert(swr, &outPtr, outSamples,
                                               (const uint8_t**)frame->data, frame->nb_samples);
                    if (converted > 0) {
                        pcmData.insert(pcmData.end(), outBuf.begin(), outBuf.begin() + converted * 4);
                    }
                }
            }
        }
        av_packet_unref(pkt);
    }

    swr_convert(swr, nullptr, 0, nullptr, 0);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    av_channel_layout_uninit(&inLayout);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    fs::remove(inputPath);

    if (pcmData.empty()) return 0;

    // Build WAV header
    std::vector<uint8_t> wavData;
    uint32_t dataSize = pcmData.size();
    uint32_t fileSize = 36 + dataSize;

    wavData.insert(wavData.end(), {'R', 'I', 'F', 'F'});
    wavData.insert(wavData.end(), (uint8_t*)&fileSize, (uint8_t*)&fileSize + 4);
    wavData.insert(wavData.end(), {'W', 'A', 'V', 'E'});
    wavData.insert(wavData.end(), {'f', 'm', 't', ' '});
    uint32_t fmtSize = 16;
    wavData.insert(wavData.end(), (uint8_t*)&fmtSize, (uint8_t*)&fmtSize + 4);
    uint16_t audioFormat = 1;
    wavData.insert(wavData.end(), (uint8_t*)&audioFormat, (uint8_t*)&audioFormat + 2);
    uint16_t numChannels = 2;
    wavData.insert(wavData.end(), (uint8_t*)&numChannels, (uint8_t*)&numChannels + 2);
    uint32_t sampleRate = 44100;
    wavData.insert(wavData.end(), (uint8_t*)&sampleRate, (uint8_t*)&sampleRate + 4);
    uint32_t byteRate = 44100 * 2 * 2;
    wavData.insert(wavData.end(), (uint8_t*)&byteRate, (uint8_t*)&byteRate + 4);
    uint16_t blockAlign = 4;
    wavData.insert(wavData.end(), (uint8_t*)&blockAlign, (uint8_t*)&blockAlign + 2);
    uint16_t bitsPerSample = 16;
    wavData.insert(wavData.end(), (uint8_t*)&bitsPerSample, (uint8_t*)&bitsPerSample + 2);
    wavData.insert(wavData.end(), {'d', 'a', 't', 'a'});
    wavData.insert(wavData.end(), (uint8_t*)&dataSize, (uint8_t*)&dataSize + 4);
    wavData.insert(wavData.end(), pcmData.begin(), pcmData.end());

    HSAMPLE sample = BASS_SampleLoad(TRUE, wavData.data(), 0, wavData.size(), 65535, BASS_SAMPLE_OVER_POS);
    return sample;
}
#endif
