#include "AudioManager.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <SDL3/SDL.h>

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

AudioManager::AudioManager()
    : decodeStream(0), tempoStream(0), initialized(false),
      currentVolume(100), sampleVolume(100), playbackRate(1.0f),
      changePitch(false), loopMusic(false), nextSampleHandle(1) {}

AudioManager::~AudioManager() {
    clearSamples();
    if (tempoStream) BASS_StreamFree(tempoStream);
    if (initialized) BASS_Free();
}

bool AudioManager::init() {
    if (!BASS_Init(-1, 44100, 0, 0, nullptr)) {
        std::cerr << "BASS_Init failed: " << BASS_ErrorGetCode() << std::endl;
        return false;
    }
    initialized = true;
    std::cout << "BASS initialized successfully" << std::endl;
    return true;
}

bool AudioManager::loadMusic(const std::string& filepath, bool loop) {
    // Clean up previous streams
    if (tempoStream) {
        BASS_StreamFree(tempoStream);
        tempoStream = 0;
        decodeStream = 0;  // BASS_FX_FREESOURCE frees this automatically
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
    DWORD tempoFlags = BASS_FX_FREESOURCE;
    if (loop) tempoFlags |= BASS_SAMPLE_LOOP;

    tempoStream = BASS_FX_TempoCreate(decodeStream, tempoFlags);
    if (!tempoStream) {
        std::cerr << "BASS_FX_TempoCreate failed: " << BASS_ErrorGetCode() << std::endl;
        BASS_StreamFree(decodeStream);
        decodeStream = 0;
        return false;
    }

    // Optimize SoundTouch parameters for better quality
    BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO_OPTION_USE_QUICKALGO, 0);
    BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO_OPTION_USE_AA_FILTER, 1);
    BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO_OPTION_AA_FILTER_LENGTH, 64);
    BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO_OPTION_SEQUENCE_MS, 82);
    BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO_OPTION_SEEKWINDOW_MS, 14);
    BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO_OPTION_OVERLAP_MS, 12);
    BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO_OPTION_PREVENT_CLICK, 1);

    // Apply current settings
    setVolume(currentVolume);
    setPlaybackRate(playbackRate);

    std::cout << "Loaded music: " << filepath << std::endl;
    return true;
}

void AudioManager::play() {
    if (tempoStream) {
        BASS_ChannelPlay(tempoStream, FALSE);
    }
}

void AudioManager::stop() {
    if (tempoStream) {
        BASS_ChannelStop(tempoStream);
        BASS_ChannelSetPosition(tempoStream, 0, BASS_POS_BYTE);
    }
}

void AudioManager::fadeIn(int ms) {
    if (tempoStream) {
        BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_VOL, 0);
        BASS_ChannelPlay(tempoStream, FALSE);
        BASS_ChannelSlideAttribute(tempoStream, BASS_ATTRIB_VOL, currentVolume / 100.0f, ms);
    }
}

void AudioManager::fadeOut(int ms) {
    if (tempoStream) {
        BASS_ChannelSlideAttribute(tempoStream, BASS_ATTRIB_VOL, 0, ms);
    }
}

void AudioManager::pause() {
    if (tempoStream) {
        BASS_ChannelPause(tempoStream);
    }
}

void AudioManager::resume() {
    if (tempoStream) {
        BASS_ChannelPlay(tempoStream, FALSE);
    }
}

int64_t AudioManager::getPosition() const {
    if (!tempoStream) return 0;

    // BASS_FX tempo stream: BASS_ChannelGetPosition returns source position, not output
    // So no need to scale by playbackRate
    QWORD pos = BASS_ChannelGetPosition(tempoStream, BASS_POS_BYTE);
    double seconds = BASS_ChannelBytes2Seconds(tempoStream, pos);
    return static_cast<int64_t>(seconds * 1000.0);
}

void AudioManager::setPosition(int64_t ms) {
    if (!tempoStream) return;

    // Convert milliseconds to bytes and set position on source stream
    double seconds = ms / 1000.0;
    HSTREAM source = BASS_FX_TempoGetSource(tempoStream);
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
        BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_VOL, currentVolume / 100.0f);
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
            devices.push_back(info.name);
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

int AudioManager::loadSample(const std::string& filepath) {
    if (!initialized) return -1;

    // Load sample with max 65535 simultaneous playbacks
    // BASS_SAMPLE_OVER_POS: when max is reached, override the oldest playing instance
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
    // Linux: if BASS fails with format error, try FFmpeg transcoding
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

void AudioManager::playSample(int handle, int volume) {
    auto it = sampleCache.find(handle);
    if (it == sampleCache.end()) return;

    HCHANNEL channel = BASS_SampleGetChannel(it->second, BASS_SAMCHAN_NEW);
    if (!channel) return;

    // Apply volume
    float finalVolume = (volume / 100.0f) * (sampleVolume / 100.0f);
    BASS_ChannelSetAttribute(channel, BASS_ATTRIB_VOL, finalVolume);

    // Apply tempo by changing frequency (affects both speed and pitch)
    // For samples, this is the only way to change playback speed
    if (playbackRate != 1.0f) {
        float origFreq;
        BASS_ChannelGetAttribute(channel, BASS_ATTRIB_FREQ, &origFreq);
        BASS_ChannelSetAttribute(channel, BASS_ATTRIB_FREQ, origFreq * playbackRate);
    }

    BASS_ChannelPlay(channel, FALSE);
}

void AudioManager::pauseAllSamples() {
    for (auto& pair : sampleCache) {
        // Get all channels for this sample
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
    for (auto& pair : sampleCache) {
        // Get all channels for this sample
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
    for (auto& pair : sampleCache) {
        BASS_SampleStop(pair.second);
    }
}

void AudioManager::warmupSamples() {
    // Pre-play all samples at zero volume to initialize BASS internal buffers
    // This prevents the "cold start" delay on first playback
    std::vector<HCHANNEL> channels;

    // Start playing all samples at zero volume
    for (auto& pair : sampleCache) {
        HCHANNEL channel = BASS_SampleGetChannel(pair.second, BASS_SAMCHAN_NEW);
        if (channel) {
            BASS_ChannelSetAttribute(channel, BASS_ATTRIB_VOL, 0.0f);
            BASS_ChannelPlay(channel, FALSE);
            channels.push_back(channel);
        }
    }

    // Let them play briefly to initialize buffers
    if (!channels.empty()) {
        SDL_Delay(50);  // 50ms should be enough to initialize
    }

    // Stop all channels
    for (HCHANNEL channel : channels) {
        BASS_ChannelStop(channel);
    }
}

void AudioManager::clearSamples() {
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

void AudioManager::setPlaybackRate(float rate) {
    playbackRate = rate;
    if (tempoStream) {
        // BASS_ATTRIB_TEMPO: -95% to +5000%, where 0 = original speed
        // rate 1.5 = +50%, rate 0.75 = -25%
        float tempo = (rate - 1.0f) * 100.0f;
        BASS_ChannelSetAttribute(tempoStream, BASS_ATTRIB_TEMPO, tempo);

        // Apply pitch change if enabled (Nightcore mode)
        if (changePitch) {
            // Calculate semitones from rate
            // 12 semitones = 1 octave = 2x frequency
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
    // Re-apply playback rate to update pitch setting
    if (tempoStream) {
        setPlaybackRate(playbackRate);
    }
}

bool AudioManager::getChangePitch() const {
    return changePitch;
}

#ifndef _WIN32
HSAMPLE AudioManager::loadSampleWithFFmpeg(const void* data, size_t size) {
    // Write to temp file (FFmpeg needs file input for memory buffer workaround)
    fs::path tempDir = fs::current_path() / "Data" / "Tmp" / "ffmpeg";
    fs::create_directories(tempDir);

    static int tempCounter = 0;
    std::string inputPath = (tempDir / ("in_" + std::to_string(tempCounter++) + ".wav")).string();

    {
        std::ofstream out(inputPath, std::ios::binary);
        if (!out) return 0;
        out.write(static_cast<const char*>(data), size);
    }

    // Open input
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

    // Find audio stream
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

    // Setup resampler to convert to S16 stereo 44100Hz
    SwrContext* swr = swr_alloc();
    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    AVChannelLayout inLayout;
    av_channel_layout_copy(&inLayout, &codecCtx->ch_layout);

    swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_S16, 44100,
                        &inLayout, codecCtx->sample_fmt, codecCtx->sample_rate, 0, nullptr);
    swr_init(swr);

    // Decode and resample
    std::vector<uint8_t> pcmData;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    while (av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index == audioIdx) {
            if (avcodec_send_packet(codecCtx, pkt) >= 0) {
                while (avcodec_receive_frame(codecCtx, frame) >= 0) {
                    int outSamples = swr_get_out_samples(swr, frame->nb_samples);
                    std::vector<uint8_t> outBuf(outSamples * 4); // stereo S16 = 4 bytes/sample
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

    // Flush
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

    // RIFF header
    wavData.insert(wavData.end(), {'R', 'I', 'F', 'F'});
    wavData.insert(wavData.end(), (uint8_t*)&fileSize, (uint8_t*)&fileSize + 4);
    wavData.insert(wavData.end(), {'W', 'A', 'V', 'E'});

    // fmt chunk
    wavData.insert(wavData.end(), {'f', 'm', 't', ' '});
    uint32_t fmtSize = 16;
    wavData.insert(wavData.end(), (uint8_t*)&fmtSize, (uint8_t*)&fmtSize + 4);
    uint16_t audioFormat = 1; // PCM
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

    // data chunk
    wavData.insert(wavData.end(), {'d', 'a', 't', 'a'});
    wavData.insert(wavData.end(), (uint8_t*)&dataSize, (uint8_t*)&dataSize + 4);
    wavData.insert(wavData.end(), pcmData.begin(), pcmData.end());

    // Load with BASS
    HSAMPLE sample = BASS_SampleLoad(TRUE, wavData.data(), 0, wavData.size(), 65535, BASS_SAMPLE_OVER_POS);
    return sample;
}
#endif
