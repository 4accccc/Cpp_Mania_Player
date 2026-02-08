#pragma once
#include <string>
#include <cstdint>
#include <SDL3/SDL.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    // Initialize with SDL renderer
    void init(SDL_Renderer* renderer);

    // Load video file
    bool load(const std::string& filepath);

    // Update video frame based on current time (milliseconds)
    void update(int64_t currentTime);

    // Get current frame texture
    SDL_Texture* getTexture() const { return texture_; }

    // Check if video is loaded
    bool isLoaded() const { return loaded_; }

    // Get video duration in milliseconds
    int64_t getDuration() const { return duration_; }

    // Get video dimensions
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }

    // Reset playback position
    void reset();

    // Close and release resources
    void close();

private:
    bool decodeFrame();
    void convertFrame();

    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;

    // FFmpeg contexts
    AVFormatContext* formatCtx_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    SwsContext* swsCtx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVFrame* frameRGB_ = nullptr;
    AVPacket* packet_ = nullptr;
    uint8_t* buffer_ = nullptr;

    int videoStreamIndex_ = -1;
    int width_ = 0;
    int height_ = 0;
    int64_t duration_ = 0;  // milliseconds
    double timeBase_ = 0.0;  // seconds per PTS unit

    int64_t currentPts_ = 0;  // Current presentation timestamp
    int64_t startTime_ = 0;   // Stream start time
    bool loaded_ = false;
    bool finished_ = false;
};
