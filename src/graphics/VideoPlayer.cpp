#include "VideoPlayer.h"
#include <iostream>

VideoPlayer::VideoPlayer() {
}

VideoPlayer::~VideoPlayer() {
    close();
}

void VideoPlayer::init(SDL_Renderer* renderer) {
    renderer_ = renderer;
}

bool VideoPlayer::load(const std::string& filepath) {
    close();  // Clean up any previous video

    // Open video file
    if (avformat_open_input(&formatCtx_, filepath.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "VideoPlayer: Failed to open file: " << filepath << std::endl;
        return false;
    }

    // Get stream info
    if (avformat_find_stream_info(formatCtx_, nullptr) < 0) {
        std::cerr << "VideoPlayer: Failed to find stream info" << std::endl;
        close();
        return false;
    }

    // Find video stream
    videoStreamIndex_ = -1;
    for (unsigned int i = 0; i < formatCtx_->nb_streams; i++) {
        if (formatCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex_ = i;
            break;
        }
    }

    if (videoStreamIndex_ < 0) {
        std::cerr << "VideoPlayer: No video stream found" << std::endl;
        close();
        return false;
    }

    AVStream* videoStream = formatCtx_->streams[videoStreamIndex_];
    AVCodecParameters* codecPar = videoStream->codecpar;

    // Find decoder
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        std::cerr << "VideoPlayer: Unsupported codec" << std::endl;
        close();
        return false;
    }

    // Create codec context
    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        std::cerr << "VideoPlayer: Failed to allocate codec context" << std::endl;
        close();
        return false;
    }

    if (avcodec_parameters_to_context(codecCtx_, codecPar) < 0) {
        std::cerr << "VideoPlayer: Failed to copy codec parameters" << std::endl;
        close();
        return false;
    }

    // Open codec
    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        std::cerr << "VideoPlayer: Failed to open codec" << std::endl;
        close();
        return false;
    }

    width_ = codecCtx_->width;
    height_ = codecCtx_->height;

    // Calculate time base and duration
    timeBase_ = av_q2d(videoStream->time_base);
    startTime_ = (videoStream->start_time != AV_NOPTS_VALUE) ? videoStream->start_time : 0;

    if (videoStream->duration != AV_NOPTS_VALUE) {
        duration_ = static_cast<int64_t>(videoStream->duration * timeBase_ * 1000.0);
    } else if (formatCtx_->duration != AV_NOPTS_VALUE) {
        duration_ = formatCtx_->duration / 1000;  // AV_TIME_BASE is microseconds
    }

    // Allocate frames
    frame_ = av_frame_alloc();
    frameRGB_ = av_frame_alloc();
    packet_ = av_packet_alloc();

    if (!frame_ || !frameRGB_ || !packet_) {
        std::cerr << "VideoPlayer: Failed to allocate frames" << std::endl;
        close();
        return false;
    }

    // Allocate buffer for RGB frame
    int bufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, width_, height_, 1);
    buffer_ = (uint8_t*)av_malloc(bufferSize);
    av_image_fill_arrays(frameRGB_->data, frameRGB_->linesize, buffer_,
                         AV_PIX_FMT_RGBA, width_, height_, 1);

    // Create scaler context
    swsCtx_ = sws_getContext(width_, height_, codecCtx_->pix_fmt,
                             width_, height_, AV_PIX_FMT_RGBA,
                             SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!swsCtx_) {
        std::cerr << "VideoPlayer: Failed to create scaler context" << std::endl;
        close();
        return false;
    }

    // Create SDL texture
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                 SDL_TEXTUREACCESS_STREAMING, width_, height_);
    if (!texture_) {
        std::cerr << "VideoPlayer: Failed to create texture" << std::endl;
        close();
        return false;
    }

    loaded_ = true;
    finished_ = false;
    currentPts_ = startTime_;

    std::cout << "VideoPlayer: Loaded " << width_ << "x" << height_
              << ", duration=" << duration_ << "ms" << std::endl;

    return true;
}

void VideoPlayer::update(int64_t currentTime) {
    if (!loaded_ || finished_) return;

    // Convert milliseconds to PTS
    int64_t targetPts = startTime_ + static_cast<int64_t>(currentTime / (timeBase_ * 1000.0));

    // Decode frames until we reach the target time
    while (currentPts_ < targetPts && !finished_) {
        if (!decodeFrame()) {
            finished_ = true;
            break;
        }
    }
}

bool VideoPlayer::decodeFrame() {
    while (av_read_frame(formatCtx_, packet_) >= 0) {
        if (packet_->stream_index == videoStreamIndex_) {
            int ret = avcodec_send_packet(codecCtx_, packet_);
            if (ret < 0) {
                av_packet_unref(packet_);
                continue;
            }

            ret = avcodec_receive_frame(codecCtx_, frame_);
            if (ret == 0) {
                currentPts_ = frame_->pts;
                convertFrame();
                av_packet_unref(packet_);
                return true;
            }
        }
        av_packet_unref(packet_);
    }
    return false;
}

void VideoPlayer::convertFrame() {
    // Convert to RGBA
    sws_scale(swsCtx_, frame_->data, frame_->linesize, 0, height_,
              frameRGB_->data, frameRGB_->linesize);

    // Update SDL texture
    SDL_UpdateTexture(texture_, nullptr, frameRGB_->data[0], frameRGB_->linesize[0]);
}

void VideoPlayer::reset() {
    if (!loaded_) return;

    // Seek to beginning
    av_seek_frame(formatCtx_, videoStreamIndex_, startTime_, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codecCtx_);
    currentPts_ = startTime_;
    finished_ = false;
}

void VideoPlayer::close() {
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    if (buffer_) {
        av_free(buffer_);
        buffer_ = nullptr;
    }
    if (packet_) {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }
    if (frameRGB_) {
        av_frame_free(&frameRGB_);
        frameRGB_ = nullptr;
    }
    if (frame_) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
    if (formatCtx_) {
        avformat_close_input(&formatCtx_);
        formatCtx_ = nullptr;
    }

    videoStreamIndex_ = -1;
    width_ = 0;
    height_ = 0;
    duration_ = 0;
    loaded_ = false;
    finished_ = false;
}
