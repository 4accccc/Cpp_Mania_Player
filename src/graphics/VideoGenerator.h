#pragma once
#include <string>
#include <cstdint>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include "ReplayParser.h"
#include "OsuParser.h"
#include "Settings.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

enum class VideoGenState {
    Idle,
    Preparing,
    Rendering,
    Encoding,
    Completed,
    Failed,
    Cancelled
};

// Judgement result for video rendering
struct NoteJudgement {
    int noteIndex;
    int judgement;  // 0=MAX, 1=300, 2=200, 3=100, 4=50, -1=miss
};

struct VideoConfig {
    int width = 540;
    int height = 960;
    int fps = 60;
    int speed = 20;           // pixels per frame
    int blockHeight = 40;     // note height
    int actionHeight = 7;     // key press height
    int stroke = 3;           // border width
    double clockRate = 1.0;   // 1.0 normal, 1.5 DT/NC, 0.75 HT
    bool isNightcore = false; // true for NC (pitch change), false for DT (no pitch change)
    std::string outputPath;
    std::string audioPath;
    bool includeAudio = true;
    bool showHolding = false; // show dashed line for all key holds
};

class VideoGenerator {
public:
    VideoGenerator();
    ~VideoGenerator();

    bool startGeneration(const ReplayInfo& replay, const BeatmapInfo& beatmap,
                         const Settings& settings, const VideoConfig& config,
                         const std::string& tempDir);
    void cancel();

    VideoGenState getState() const { return state_.load(); }
    float getProgress() const { return progress_.load(); }
    bool isRunning() const;
    std::string getStatusText() const;
    std::string getErrorMessage() const { return errorMessage_; }
    std::string getOutputPath() const { return outputPath_; }

private:
    void workerThread();
    bool initEncoder();
    bool encodeFrame(uint8_t* pixels, int64_t pts);
    bool finishEncoding();
    void cleanup();
    bool muxAudio();  // Mix audio into video

    // Rendering helpers
    int timeToY(int64_t time) const;
    void renderLongImage(std::vector<uint8_t>& pixels);
    void renderLongImageRegion(std::vector<uint8_t>& pixels, int regionTop, int regionHeight);
    void getJudgementColor(int judgement, uint8_t& r, uint8_t& g, uint8_t& b);
    void setPixel(uint8_t* p, int imgW, int imgH, int x, int y,
                  uint8_t r, uint8_t g, uint8_t b);
    void fillRect(uint8_t* p, int imgW, int imgH, int x, int y, int w, int h,
                  uint8_t r, uint8_t g, uint8_t b);
    void drawRect(uint8_t* p, int imgW, int imgH, int x, int y, int w, int h,
                  int stroke, uint8_t r, uint8_t g, uint8_t b);

    // Judgement calculation
    void calculateJudgements();
    int getJudgementFromDiff(int64_t diff) const;

    // Thread-safe state
    std::atomic<VideoGenState> state_{VideoGenState::Idle};
    std::atomic<float> progress_{0.0f};
    std::atomic<bool> cancelRequested_{false};

    std::thread workerThread_;
    mutable std::mutex mutex_;

    // Generation parameters
    ReplayInfo replay_;
    BeatmapInfo beatmap_;
    Settings settings_;
    VideoConfig config_;
    std::string tempDir_;
    std::string outputPath_;
    std::string tempVideoPath_;  // Temporary video file (without audio)
    std::string errorMessage_;

    // FFmpeg encoding
    AVFormatContext* formatCtx_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    AVStream* videoStream_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* swsCtx_ = nullptr;

    // Calculated parameters
    int64_t duration_;
    int totalHeight_;
    int columnWidth_;
    double timeHeightRatio_;

    // Judgement results
    std::vector<int> noteJudgements_;  // judgement for each note
};
