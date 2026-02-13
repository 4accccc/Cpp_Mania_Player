#include "VideoGenerator.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace fs = std::filesystem;

VideoGenerator::VideoGenerator() {}

VideoGenerator::~VideoGenerator() {
    cancel();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    cleanup();
}

bool VideoGenerator::startGeneration(const ReplayInfo& replay, const BeatmapInfo& beatmap,
                                     const Settings& settings, const VideoConfig& config,
                                     const std::string& tempDir) {
    if (isRunning()) {
        errorMessage_ = "Video generation already in progress";
        return false;
    }
    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    replay_ = replay;
    beatmap_ = beatmap;
    settings_ = settings;
    config_ = config;
    tempDir_ = tempDir;
    outputPath_ = config.outputPath;
    errorMessage_.clear();
    cancelRequested_ = false;
    progress_ = 0.0f;
    state_ = VideoGenState::Preparing;

    workerThread_ = std::thread(&VideoGenerator::workerThread, this);
    return true;
}

void VideoGenerator::cancel() { cancelRequested_ = true; }

bool VideoGenerator::isRunning() const {
    VideoGenState s = state_.load();
    return s != VideoGenState::Idle && s != VideoGenState::Completed &&
           s != VideoGenState::Failed && s != VideoGenState::Cancelled;
}

std::string VideoGenerator::getStatusText() const {
    switch (state_.load()) {
        case VideoGenState::Idle: return "Idle";
        case VideoGenState::Preparing: return "Preparing...";
        case VideoGenState::Rendering: return "Rendering...";
        case VideoGenState::Encoding: return "Encoding...";
        case VideoGenState::Completed: return "Completed!";
        case VideoGenState::Failed: return "Failed: " + errorMessage_;
        case VideoGenState::Cancelled: return "Cancelled";
        default: return "Unknown";
    }
}

void VideoGenerator::cleanup() {
    if (packet_) { av_packet_free(&packet_); packet_ = nullptr; }
    if (frame_) { av_frame_free(&frame_); frame_ = nullptr; }
    if (swsCtx_) { sws_freeContext(swsCtx_); swsCtx_ = nullptr; }
    if (codecCtx_) { avcodec_free_context(&codecCtx_); codecCtx_ = nullptr; }
    if (formatCtx_) {
        if (formatCtx_->pb) avio_closep(&formatCtx_->pb);
        avformat_free_context(formatCtx_);
        formatCtx_ = nullptr;
    }
    videoStream_ = nullptr;
}

void VideoGenerator::workerThread() {
    // Calculate duration (adjusted for clock rate)
    // Add 1000ms delay at start, 2000ms at end (like Mania-Replay-Master)
    const int64_t START_DELAY = 1000;  // Not affected by clock rate
    const int64_t END_DELAY = 2000;

    int64_t lastTime = 0;
    for (const auto& note : beatmap_.notes) {
        int64_t endT = note.isHold ? note.endTime : note.time;
        lastTime = std::max(lastTime, endT);
    }
    // Only song duration is affected by clock rate, not the delays
    duration_ = START_DELAY + (int64_t)((lastTime + END_DELAY) / config_.clockRate);

    // Calculate layout
    columnWidth_ = config_.width / beatmap_.keyCount;
    timeHeightRatio_ = (double)config_.speed / 1000.0 * config_.fps;
    totalHeight_ = (int)std::ceil(duration_ * timeHeightRatio_) + config_.height;

    // Calculate judgements
    calculateJudgements();

    state_ = VideoGenState::Rendering;

    // Initialize encoder early (needed for segmented path)
    if (!initEncoder()) { state_ = VideoGenState::Failed; cleanup(); return; }

    int64_t totalFrames = (duration_ * config_.fps) / 1000;
    std::vector<uint8_t> framePixels(config_.width * config_.height * 3);

    // Determine if we need segmented rendering
    const int64_t MAX_SEGMENT_BYTES = 256LL * 1024 * 1024; // 256MB per segment
    int64_t totalImageBytes = (int64_t)config_.width * totalHeight_ * 3;

    if (totalImageBytes <= MAX_SEGMENT_BYTES) {
        // Small enough - single allocation (existing fast path)
        std::vector<uint8_t> longImage(totalImageBytes, 0);
        renderLongImageRegion(longImage, 0, totalHeight_);

        if (cancelRequested_) { state_ = VideoGenState::Cancelled; cleanup(); return; }

        state_ = VideoGenState::Encoding;
        for (int64_t f = 0; f < totalFrames && !cancelRequested_; f++) {
            int scrollY = (int)(f * config_.speed);
            int srcY = totalHeight_ - config_.height - scrollY;
            if (srcY < 0) srcY = 0;

            std::memset(framePixels.data(), 0, framePixels.size());
            for (int y = 0; y < config_.height; y++) {
                int imgY = srcY + y;
                if (imgY >= 0 && imgY < totalHeight_) {
                    std::memcpy(&framePixels[y * config_.width * 3],
                               &longImage[imgY * config_.width * 3],
                               config_.width * 3);
                }
            }

            if (!encodeFrame(framePixels.data(), f)) {
                errorMessage_ = "Failed to encode frame";
                state_ = VideoGenState::Failed; cleanup(); return;
            }
            progress_ = 0.3f + 0.7f * f / totalFrames;
        }
    } else {
        // Segmented rendering for long beatmaps
        int segMaxHeight = (int)(MAX_SEGMENT_BYTES / ((int64_t)config_.width * 3));
        // Ensure segment is at least 2x frame height for proper overlap
        segMaxHeight = std::max(segMaxHeight, config_.height * 3);
        int overlap = config_.height;  // overlap to handle frames at segment boundaries
        int step = segMaxHeight - overlap;

        std::cout << "VideoGenerator: Using segmented rendering (totalHeight=" << totalHeight_
                  << ", segHeight=" << segMaxHeight << ", ~"
                  << (totalImageBytes / (1024 * 1024)) << "MB total)" << std::endl;

        state_ = VideoGenState::Encoding;
        int64_t currentFrame = 0;

        // Frames scroll from bottom to top: frame 0 reads near Y=totalHeight, last frame near Y=0
        // Process segments from bottom to top
        for (int segTop = std::max(0, totalHeight_ - segMaxHeight);
             currentFrame < totalFrames && !cancelRequested_;
             segTop -= step) {

            if (segTop < 0) segTop = 0;
            int segBottom = std::min(segTop + segMaxHeight, totalHeight_);
            int segHeight = segBottom - segTop;

            // Render this segment
            std::vector<uint8_t> segPixels((int64_t)config_.width * segHeight * 3, 0);
            renderLongImageRegion(segPixels, segTop, segHeight);

            // Encode frames whose viewport falls within this segment
            while (currentFrame < totalFrames && !cancelRequested_) {
                int scrollY = (int)(currentFrame * config_.speed);
                int srcY = totalHeight_ - config_.height - scrollY;
                if (srcY < 0) srcY = 0;

                // If viewport top is above this segment, we need the next segment (further up)
                if (srcY < segTop) break;

                // Copy from segment buffer (srcY is in absolute coords, offset to segment)
                std::memset(framePixels.data(), 0, framePixels.size());
                for (int y = 0; y < config_.height; y++) {
                    int absY = srcY + y;
                    int segY = absY - segTop;
                    if (segY >= 0 && segY < segHeight) {
                        std::memcpy(&framePixels[y * config_.width * 3],
                                   &segPixels[segY * config_.width * 3],
                                   config_.width * 3);
                    }
                }

                if (!encodeFrame(framePixels.data(), currentFrame)) {
                    errorMessage_ = "Failed to encode frame";
                    state_ = VideoGenState::Failed; cleanup(); return;
                }
                progress_ = 0.3f + 0.7f * currentFrame / totalFrames;
                currentFrame++;
            }

            // If we've reached the top of the image, no more segments needed
            if (segTop == 0) break;
        }
    }

    if (cancelRequested_) { state_ = VideoGenState::Cancelled; cleanup(); return; }

    if (!finishEncoding()) {
        errorMessage_ = "Failed to finish encoding";
        state_ = VideoGenState::Failed; cleanup(); return;
    }

    cleanup();

    // Mux audio if available
    if (config_.includeAudio && !config_.audioPath.empty()) {
        state_ = VideoGenState::Encoding;
        if (!muxAudio()) {
            // muxAudio() already set detailed errorMessage_
            state_ = VideoGenState::Failed;
            return;
        }
    }

    progress_ = 1.0f;
    state_ = VideoGenState::Completed;
}

bool VideoGenerator::initEncoder() {
    // Use temp file if we need to mux audio later
    if (config_.includeAudio && !config_.audioPath.empty()) {
        tempVideoPath_ = tempDir_ + "/temp_video.mp4";
    } else {
        tempVideoPath_ = outputPath_;
    }

    int ret = avformat_alloc_output_context2(&formatCtx_, nullptr, nullptr, tempVideoPath_.c_str());
    if (ret < 0 || !formatCtx_) {
        errorMessage_ = "Failed to create output context";
        return false;
    }

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        errorMessage_ = "H.264 encoder not found";
        return false;
    }

    videoStream_ = avformat_new_stream(formatCtx_, nullptr);
    if (!videoStream_) {
        errorMessage_ = "Failed to create video stream";
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        errorMessage_ = "Failed to allocate codec context";
        return false;
    }

    codecCtx_->width = config_.width;
    codecCtx_->height = config_.height;
    codecCtx_->time_base = {1, config_.fps};
    codecCtx_->framerate = {config_.fps, 1};
    codecCtx_->pix_fmt = AV_PIX_FMT_YUV420P;
    codecCtx_->bit_rate = 4000000;
    codecCtx_->gop_size = 12;
    codecCtx_->max_b_frames = 2;

    av_opt_set(codecCtx_->priv_data, "preset", "medium", 0);
    if (formatCtx_->oformat->flags & AVFMT_GLOBALHEADER)
        codecCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_open2(codecCtx_, codec, nullptr);
    if (ret < 0) { errorMessage_ = "Failed to open codec"; return false; }

    ret = avcodec_parameters_from_context(videoStream_->codecpar, codecCtx_);
    if (ret < 0) { errorMessage_ = "Failed to copy codec params"; return false; }
    videoStream_->time_base = codecCtx_->time_base;

    if (!(formatCtx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&formatCtx_->pb, tempVideoPath_.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) { errorMessage_ = "Failed to open output file"; return false; }
    }

    ret = avformat_write_header(formatCtx_, nullptr);
    if (ret < 0) { errorMessage_ = "Failed to write header"; return false; }

    frame_ = av_frame_alloc();
    frame_->format = codecCtx_->pix_fmt;
    frame_->width = config_.width;
    frame_->height = config_.height;
    av_frame_get_buffer(frame_, 0);

    packet_ = av_packet_alloc();

    swsCtx_ = sws_getContext(config_.width, config_.height, AV_PIX_FMT_RGB24,
                             config_.width, config_.height, AV_PIX_FMT_YUV420P,
                             SWS_BILINEAR, nullptr, nullptr, nullptr);
    return swsCtx_ != nullptr;
}

bool VideoGenerator::encodeFrame(uint8_t* pixels, int64_t pts) {
    av_frame_make_writable(frame_);

    const uint8_t* srcSlice[1] = { pixels };
    int srcStride[1] = { config_.width * 3 };
    sws_scale(swsCtx_, srcSlice, srcStride, 0, config_.height,
              frame_->data, frame_->linesize);
    frame_->pts = pts;

    int ret = avcodec_send_frame(codecCtx_, frame_);
    if (ret < 0) return false;

    while (ret >= 0) {
        ret = avcodec_receive_packet(codecCtx_, packet_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) return false;

        av_packet_rescale_ts(packet_, codecCtx_->time_base, videoStream_->time_base);
        packet_->stream_index = videoStream_->index;
        ret = av_interleaved_write_frame(formatCtx_, packet_);
        av_packet_unref(packet_);
        if (ret < 0) return false;
    }
    return true;
}

bool VideoGenerator::finishEncoding() {
    avcodec_send_frame(codecCtx_, nullptr);
    int ret = 0;
    while (ret >= 0) {
        ret = avcodec_receive_packet(codecCtx_, packet_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) return false;
        av_packet_rescale_ts(packet_, codecCtx_->time_base, videoStream_->time_base);
        packet_->stream_index = videoStream_->index;
        av_interleaved_write_frame(formatCtx_, packet_);
        av_packet_unref(packet_);
    }
    av_write_trailer(formatCtx_);
    return true;
}

bool VideoGenerator::muxAudio() {
    // Check if temp video file exists
    if (!fs::exists(tempVideoPath_)) {
        errorMessage_ = "Temp video not found: " + tempVideoPath_;
        return false;
    }

    // Check if audio file exists - if not, just copy video without audio
    if (!fs::exists(config_.audioPath)) {
        // No audio file, just rename temp video to output
        fs::rename(tempVideoPath_, outputPath_);
        return true;
    }

    // If clock rate != 1.0, we need to adjust audio speed using ffmpeg command
    std::string audioToUse = config_.audioPath;
    std::string tempAudioPath;

    if (std::abs(config_.clockRate - 1.0) > 0.01) {
        // Create temp audio file path
        tempAudioPath = tempDir_ + "/temp_audio.mp3";

        std::string cmd;
        if (config_.isNightcore) {
            // Nightcore: change speed AND pitch using asetrate
            // asetrate changes sample rate, which affects both speed and pitch
            cmd = "ffmpeg -y -i \"" + config_.audioPath +
                  "\" -filter:a \"asetrate=44100*" + std::to_string(config_.clockRate) +
                  ",aresample=44100\" -vn \"" + tempAudioPath + "\"";
        } else {
            // DT/HT: change speed only, preserve pitch using atempo
            cmd = "ffmpeg -y -i \"" + config_.audioPath +
                  "\" -filter:a \"atempo=" + std::to_string(config_.clockRate) +
                  "\" -vn \"" + tempAudioPath + "\"";
        }

        int ret = system(cmd.c_str());
        if (ret != 0 || !fs::exists(tempAudioPath)) {
            errorMessage_ = "Failed to adjust audio speed";
            return false;
        }
        audioToUse = tempAudioPath;
    }

    // Open input video file
    AVFormatContext* videoIn = nullptr;
    if (avformat_open_input(&videoIn, tempVideoPath_.c_str(), nullptr, nullptr) < 0) {
        errorMessage_ = "Failed to open temp video: " + tempVideoPath_;
        return false;
    }
    if (avformat_find_stream_info(videoIn, nullptr) < 0) {
        errorMessage_ = "Failed to find video stream info";
        avformat_close_input(&videoIn);
        return false;
    }

    // Open input audio file
    AVFormatContext* audioIn = nullptr;
    if (avformat_open_input(&audioIn, audioToUse.c_str(), nullptr, nullptr) < 0) {
        errorMessage_ = "Failed to open audio: " + audioToUse;
        avformat_close_input(&videoIn);
        return false;
    }
    if (avformat_find_stream_info(audioIn, nullptr) < 0) {
        errorMessage_ = "Failed to find audio stream info";
        avformat_close_input(&audioIn);
        avformat_close_input(&videoIn);
        return false;
    }

    // Create output file
    AVFormatContext* output = nullptr;
    if (avformat_alloc_output_context2(&output, nullptr, nullptr, outputPath_.c_str()) < 0) {
        errorMessage_ = "Failed to create output context";
        avformat_close_input(&audioIn);
        avformat_close_input(&videoIn);
        return false;
    }

    // Find video stream
    int videoStreamIdx = -1;
    for (unsigned i = 0; i < videoIn->nb_streams; i++) {
        if (videoIn->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx = i;
            break;
        }
    }

    // Find audio stream
    int audioStreamIdx = -1;
    for (unsigned i = 0; i < audioIn->nb_streams; i++) {
        if (audioIn->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIdx = i;
            break;
        }
    }

    if (videoStreamIdx < 0 || audioStreamIdx < 0) {
        errorMessage_ = "Failed to find video/audio stream";
        avformat_free_context(output);
        avformat_close_input(&audioIn);
        avformat_close_input(&videoIn);
        return false;
    }

    // Create output video stream
    AVStream* outVideoStream = avformat_new_stream(output, nullptr);
    avcodec_parameters_copy(outVideoStream->codecpar, videoIn->streams[videoStreamIdx]->codecpar);
    outVideoStream->time_base = videoIn->streams[videoStreamIdx]->time_base;

    // Create output audio stream
    AVStream* outAudioStream = avformat_new_stream(output, nullptr);
    avcodec_parameters_copy(outAudioStream->codecpar, audioIn->streams[audioStreamIdx]->codecpar);
    outAudioStream->time_base = audioIn->streams[audioStreamIdx]->time_base;

    // Open output file
    if (!(output->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&output->pb, outputPath_.c_str(), AVIO_FLAG_WRITE) < 0) {
            errorMessage_ = "Failed to open output file";
            avformat_free_context(output);
            avformat_close_input(&audioIn);
            avformat_close_input(&videoIn);
            return false;
        }
    }

    if (avformat_write_header(output, nullptr) < 0) {
        errorMessage_ = "Failed to write header";
        avio_closep(&output->pb);
        avformat_free_context(output);
        avformat_close_input(&audioIn);
        avformat_close_input(&videoIn);
        return false;
    }

    // Copy video packets
    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(videoIn, pkt) >= 0) {
        if (pkt->stream_index == videoStreamIdx) {
            pkt->stream_index = 0;
            av_packet_rescale_ts(pkt, videoIn->streams[videoStreamIdx]->time_base, outVideoStream->time_base);
            av_interleaved_write_frame(output, pkt);
        }
        av_packet_unref(pkt);
    }

    // Copy audio packets with start delay
    const int64_t START_DELAY_MS = 1000;
    AVRational audioTimeBase = audioIn->streams[audioStreamIdx]->time_base;
    int64_t startDelayPts = av_rescale_q(START_DELAY_MS, {1, 1000}, outAudioStream->time_base);

    av_seek_frame(audioIn, audioStreamIdx, 0, AVSEEK_FLAG_BACKWARD);
    while (av_read_frame(audioIn, pkt) >= 0) {
        if (pkt->stream_index == audioStreamIdx) {
            // Rescale timestamps from input to output time base
            av_packet_rescale_ts(pkt, audioIn->streams[audioStreamIdx]->time_base, outAudioStream->time_base);
            // Add start delay
            pkt->pts += startDelayPts;
            pkt->dts += startDelayPts;
            pkt->stream_index = 1;
            av_interleaved_write_frame(output, pkt);
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    av_write_trailer(output);

    // Cleanup
    avio_closep(&output->pb);
    avformat_free_context(output);
    avformat_close_input(&audioIn);
    avformat_close_input(&videoIn);

    // Delete temp file
    fs::remove(tempVideoPath_);

    return true;
}

int VideoGenerator::getJudgementFromDiff(int64_t diff) const {
    if (diff <= 16) return 0;   // MAX
    if (diff <= 64) return 1;   // 300
    if (diff <= 97) return 2;   // 200
    if (diff <= 127) return 3;  // 100
    if (diff <= 151) return 4;  // 50
    return -1;                  // miss
}

void VideoGenerator::calculateJudgements() {
    noteJudgements_.resize(beatmap_.notes.size(), -1);
    std::vector<bool> noteHit(beatmap_.notes.size(), false);

    int prevKeyState = 0;
    for (const auto& frame : replay_.frames) {
        for (int lane = 0; lane < beatmap_.keyCount; lane++) {
            bool pressed = (frame.keyState >> lane) & 1;
            bool wasPressed = (prevKeyState >> lane) & 1;

            if (pressed && !wasPressed) {
                // Find closest unhit note in this lane
                int64_t bestDiff = INT64_MAX;
                int bestIdx = -1;

                for (size_t i = 0; i < beatmap_.notes.size(); i++) {
                    if (noteHit[i]) continue;
                    const auto& note = beatmap_.notes[i];
                    if (note.lane != lane) continue;

                    int64_t diff = std::abs(frame.time - note.time);
                    if (diff < bestDiff && diff <= 188) {
                        bestDiff = diff;
                        bestIdx = (int)i;
                    }
                }

                if (bestIdx >= 0) {
                    noteHit[bestIdx] = true;
                    noteJudgements_[bestIdx] = getJudgementFromDiff(bestDiff);
                }
            }
        }
        prevKeyState = frame.keyState;
    }
}

int VideoGenerator::timeToY(int64_t time) const {
    // Add 1000ms start delay, then adjust for clock rate (DT=1.5x, HT=0.75x)
    double adjustedTime = 1000.0 + time / config_.clockRate;
    return (int)(adjustedTime * timeHeightRatio_);
}

void VideoGenerator::getJudgementColor(int j, uint8_t& r, uint8_t& g, uint8_t& b) {
    switch (j) {
        case 0: r=255; g=255; b=255; break;  // MAX
        case 1: r=255; g=210; b=55;  break;  // 300
        case 2: r=121; g=208; b=32;  break;  // 200
        case 3: r=30;  g=104; b=197; break;  // 100
        case 4: r=225; g=52;  b=155; break;  // 50
        default: r=255; g=0; b=0; break;     // miss
    }
}

void VideoGenerator::setPixel(uint8_t* p, int imgW, int imgH, int x, int y,
                              uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= imgW || y < 0 || y >= imgH) return;
    int idx = (y * imgW + x) * 3;
    p[idx] = r; p[idx+1] = g; p[idx+2] = b;
}

void VideoGenerator::fillRect(uint8_t* p, int imgW, int imgH,
                              int x, int y, int w, int h,
                              uint8_t r, uint8_t g, uint8_t b) {
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            setPixel(p, imgW, imgH, px, py, r, g, b);
}

void VideoGenerator::drawRect(uint8_t* p, int imgW, int imgH,
                              int x, int y, int w, int h, int stroke,
                              uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < stroke; i++) {
        for (int px = x; px < x + w; px++) {
            setPixel(p, imgW, imgH, px, y + i, r, g, b);
            setPixel(p, imgW, imgH, px, y + h - 1 - i, r, g, b);
        }
        for (int py = y; py < y + h; py++) {
            setPixel(p, imgW, imgH, x + i, py, r, g, b);
            setPixel(p, imgW, imgH, x + w - 1 - i, py, r, g, b);
        }
    }
}

void VideoGenerator::renderLongImage(std::vector<uint8_t>& pixels) {
    renderLongImageRegion(pixels, 0, totalHeight_);
}

void VideoGenerator::renderLongImageRegion(std::vector<uint8_t>& pixels, int regionTop, int regionHeight) {
    int imgW = config_.width;
    int imgH = regionHeight;  // buffer height = region height
    // All absolute Y coords are offset by -regionTop before drawing.
    // setPixel/fillRect/drawRect clip to [0, imgH), so out-of-region elements are ignored.

    // Render notes with judgement colors (border only)
    for (size_t i = 0; i < beatmap_.notes.size(); i++) {
        const auto& note = beatmap_.notes[i];
        int absY = totalHeight_ - timeToY(note.time) - config_.blockHeight;
        int x = (int)(note.lane * columnWidth_ + columnWidth_ * 0.1);
        int w = (int)(columnWidth_ * 0.8);
        int h = config_.blockHeight;

        // Get color from judgement
        uint8_t r, g, b;
        getJudgementColor(noteJudgements_[i], r, g, b);

        if (note.isHold) {
            // Calculate hold note height based on duration (adjusted for clock rate)
            int64_t duration = note.endTime - note.time;
            int holdHeight = std::max(h, (int)std::ceil(duration / config_.clockRate * timeHeightRatio_));
            int holdAbsY = totalHeight_ - timeToY(note.time);
            // Draw as single rectangle from bottom to top
            drawRect(pixels.data(), imgW, imgH, x, (holdAbsY - holdHeight) - regionTop, w, holdHeight, config_.stroke, r, g, b);
        } else {
            // Draw border only (no fill)
            drawRect(pixels.data(), imgW, imgH, x, absY - regionTop, w, h, config_.stroke, r, g, b);
        }
    }
    progress_ = 0.15f;

    // Render replay actions (including hold lines)
    int prevKey = 0;
    std::vector<int64_t> holdStartTime(beatmap_.keyCount, -1);  // Track hold start time per lane
    std::vector<int> holdNoteIndex(beatmap_.keyCount, -1);      // Track which note is being held

    for (const auto& frame : replay_.frames) {
        for (int lane = 0; lane < beatmap_.keyCount; lane++) {
            bool pressed = (frame.keyState >> lane) & 1;
            bool wasPressed = (prevKey >> lane) & 1;

            if (pressed && !wasPressed) {
                // Key pressed - find matching note and draw action bar with judgement color
                int y = totalHeight_ - timeToY(frame.time) - config_.actionHeight - regionTop;
                int x = (int)(lane * columnWidth_ + columnWidth_ * 0.3);
                int w = (int)(columnWidth_ * 0.4);

                // Find the note that was hit and get its judgement color
                uint8_t r = 255, g = 0, b = 0;  // Default red (miss/empty tap)
                int matchedNoteIdx = -1;
                for (size_t i = 0; i < beatmap_.notes.size(); i++) {
                    const auto& note = beatmap_.notes[i];
                    if (note.lane == lane && std::abs(frame.time - note.time) <= 188) {
                        getJudgementColor(noteJudgements_[i], r, g, b);
                        if (note.isHold) {
                            matchedNoteIdx = (int)i;
                        }
                        break;
                    }
                }

                fillRect(pixels.data(), imgW, imgH, x, y, w, config_.actionHeight, r, g, b);
                holdStartTime[lane] = frame.time;
                holdNoteIndex[lane] = matchedNoteIdx;
            }
            else if (!pressed && wasPressed && holdStartTime[lane] >= 0) {
                // Key released
                int64_t startT = holdStartTime[lane];
                int64_t endT = frame.time;
                int noteIdx = holdNoteIndex[lane];

                // Check if this was a hold note
                bool isHoldNote = (noteIdx >= 0 && beatmap_.notes[noteIdx].isHold);

                // Draw release action bar for hold notes
                if (isHoldNote) {
                    int y = totalHeight_ - timeToY(endT) - config_.actionHeight - regionTop;
                    int x = (int)(lane * columnWidth_ + columnWidth_ * 0.3);
                    int w = (int)(columnWidth_ * 0.4);

                    // Use same judgement color as the note
                    uint8_t r, g, b;
                    getJudgementColor(noteJudgements_[noteIdx], r, g, b);
                    fillRect(pixels.data(), imgW, imgH, x, y, w, config_.actionHeight, r, g, b);
                }

                // Draw dashed line: always for hold notes, or if showHolding is enabled
                if (isHoldNote || config_.showHolding) {
                    int startY = totalHeight_ - timeToY(startT) - regionTop;
                    int endY = totalHeight_ - timeToY(endT) - regionTop;
                    int top = std::min(startY, endY);
                    int bot = std::max(startY, endY);

                    int centerX = (int)(lane * columnWidth_ + columnWidth_ * 0.5);
                    int dashW = config_.actionHeight / 2;
                    int dashH = config_.actionHeight;
                    int gap = config_.actionHeight;

                    for (int py = bot - dashH * 2; py > top + dashH; py -= (dashH + gap)) {
                        fillRect(pixels.data(), imgW, imgH, centerX - dashW, py, dashW * 2, dashH, 100, 100, 100);
                    }
                }

                holdStartTime[lane] = -1;
                holdNoteIndex[lane] = -1;
            }
        }
        prevKey = frame.keyState;
    }
    progress_ = 0.3f;
}