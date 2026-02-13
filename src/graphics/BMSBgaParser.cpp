#include "BMSBgaParser.h"
#include "BMSParser.h"
#include "stb_image.h"
#include <filesystem>
#include <iostream>
#include <algorithm>

namespace fs = std::filesystem;

bool BMSBgaManager::isVideoFile(const std::string& filename) {
    std::string ext = fs::path(filename).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".mpg" || ext == ".mpeg" || ext == ".avi" ||
           ext == ".mp4" || ext == ".wmv" || ext == ".mkv";
}

BMSBgaManager::BMSBgaManager() {
}

BMSBgaManager::~BMSBgaManager() {
    clear();
}

void BMSBgaManager::init(SDL_Renderer* renderer) {
    renderer_ = renderer;
}

void BMSBgaManager::load(const std::vector<BMSBgaEvent>& events,
                         const std::unordered_map<int, std::string>& bmpDefs,
                         const std::string& directory) {
    clear();
    events_ = events;
    bmpDefs_ = bmpDefs;
    directory_ = directory;
    currentIndex_ = 0;
    lastUpdateTime_ = INT64_MIN;
    missLayerStartTime_ = 0;

    // Reset all layers
    for (int i = 0; i < 3; i++) {
        layers_[i].active = false;
        layers_[i].currentBmpId = -1;
        layers_[i].texture = nullptr;
    }

    // Set #BMP00 as default Poor layer (miss image)
    if (bmpDefs_.count(0)) {
        // Pre-load the miss image
        mediaTypeCache_[0] = BgaMediaType::Image;
        textureCache_[0] = loadTexture(bmpDefs_[0]);
        if (textureCache_[0]) {
            layers_[2].active = true;
            layers_[2].currentBmpId = 0;
            layers_[2].texture = textureCache_[0];
            std::cout << "BMSBgaManager: Set #BMP00 as Poor layer: " << bmpDefs_[0] << std::endl;
        }
    }

    std::cout << "BMSBgaManager: Loaded " << events_.size() << " events, "
              << bmpDefs_.size() << " BMP definitions" << std::endl;
}

SDL_Texture* BMSBgaManager::loadTexture(const std::string& filename) {
    return loadTextureWithColorKey(filename, false);
}

// Load texture with optional black color key (for Layer transparency)
SDL_Texture* BMSBgaManager::loadTextureWithColorKey(const std::string& filename, bool useColorKey) {
    if (!renderer_) return nullptr;

    fs::path imgPath = fs::path(directory_) / filename;

    // Try different extensions
    if (!fs::exists(imgPath)) {
        std::string stem = imgPath.stem().string();
        fs::path parent = imgPath.parent_path();
        for (const auto& ext : {".bmp", ".png", ".jpg", ".jpeg"}) {
            fs::path tryPath = parent / (stem + ext);
            if (fs::exists(tryPath)) {
                imgPath = tryPath;
                break;
            }
        }
    }

    if (!fs::exists(imgPath)) {
        std::cerr << "BMSBgaManager: Image not found: " << imgPath.string() << std::endl;
        return nullptr;
    }

    int w, h, channels;
    unsigned char* data = stbi_load(imgPath.string().c_str(), &w, &h, &channels, 4);
    if (!data) {
        std::cerr << "BMSBgaManager: Failed to load image: " << imgPath.string() << std::endl;
        return nullptr;
    }

    // Apply color key: make black pixels transparent (for Layer)
    if (useColorKey) {
        for (int i = 0; i < w * h; i++) {
            unsigned char* pixel = data + i * 4;
            // If pixel is black (or near black), make it transparent
            if (pixel[0] < 8 && pixel[1] < 8 && pixel[2] < 8) {
                pixel[3] = 0;  // Set alpha to 0
            }
        }
    }

    SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, data, w * 4);
    SDL_Texture* texture = nullptr;
    if (surface) {
        texture = SDL_CreateTextureFromSurface(renderer_, surface);
        if (!texture) {
            std::cerr << "BMSBgaManager: SDL_CreateTextureFromSurface failed: " << SDL_GetError() << std::endl;
        } else {
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        }
        SDL_DestroySurface(surface);
    }
    stbi_image_free(data);

    return texture;
}

VideoPlayer* BMSBgaManager::loadVideo(const std::string& filename) {
    if (!renderer_) return nullptr;

    fs::path videoPath = fs::path(directory_) / filename;

    // Try different video extensions
    if (!fs::exists(videoPath)) {
        std::string stem = videoPath.stem().string();
        fs::path parent = videoPath.parent_path();
        for (const auto& ext : {".mpg", ".mpeg", ".avi", ".mp4", ".wmv", ".mkv"}) {
            fs::path tryPath = parent / (stem + ext);
            if (fs::exists(tryPath)) {
                videoPath = tryPath;
                break;
            }
        }
    }

    if (!fs::exists(videoPath)) {
        return nullptr;
    }

    VideoPlayer* player = new VideoPlayer();
    player->init(renderer_);
    if (!player->load(videoPath.string())) {
        delete player;
        return nullptr;
    }

    return player;
}

// Recalculate BGA state from scratch (for time jumps/seeks)
void BMSBgaManager::recalculateState(int64_t currentTime) {
    // Reset all layers
    for (int i = 0; i < 3; i++) {
        layers_[i].active = false;
        layers_[i].currentBmpId = -1;
        layers_[i].texture = nullptr;
    }

    // Find the last event for each layer before currentTime
    int lastBgaIdx = -1;
    int lastLayerIdx = -1;
    int lastPoorIdx = -1;

    for (size_t i = 0; i < events_.size(); i++) {
        if (events_[i].time > currentTime) break;

        if (events_[i].layer == 0) lastBgaIdx = static_cast<int>(i);
        else if (events_[i].layer == 1) lastLayerIdx = static_cast<int>(i);
        else if (events_[i].layer == 2) lastPoorIdx = static_cast<int>(i);
    }

    // Apply the last events
    auto applyEvent = [this, currentTime](int idx, int layer) {
        if (idx < 0) return;
        const auto& evt = events_[idx];

        // Load texture/video if not cached
        if (mediaTypeCache_.find(evt.bmpId) == mediaTypeCache_.end()) {
            if (bmpDefs_.count(evt.bmpId)) {
                const std::string& filename = bmpDefs_[evt.bmpId];
                if (isVideoFile(filename)) {
                    mediaTypeCache_[evt.bmpId] = BgaMediaType::Video;
                    videoCache_[evt.bmpId] = loadVideo(filename);
                } else {
                    mediaTypeCache_[evt.bmpId] = BgaMediaType::Image;
                    textureCache_[evt.bmpId] = loadTexture(filename);
                }
            } else {
                mediaTypeCache_[evt.bmpId] = BgaMediaType::Image;
                textureCache_[evt.bmpId] = nullptr;
            }
        }

        layers_[layer].active = true;
        layers_[layer].currentBmpId = evt.bmpId;
        layers_[layer].mediaType = mediaTypeCache_[evt.bmpId];

        if (layers_[layer].mediaType == BgaMediaType::Video) {
            if (videoCache_.count(evt.bmpId) && videoCache_[evt.bmpId]) {
                videoCache_[evt.bmpId]->reset();
                layers_[layer].videoStartTime = evt.time;
                // Seek video to correct position
                int64_t videoTime = currentTime - evt.time;
                if (videoTime > 0) {
                    videoCache_[evt.bmpId]->update(videoTime);
                }
                layers_[layer].texture = videoCache_[evt.bmpId]->getTexture();
            }
        } else {
            layers_[layer].texture = textureCache_[evt.bmpId];
        }
    };

    applyEvent(lastBgaIdx, 0);
    applyEvent(lastLayerIdx, 1);
    applyEvent(lastPoorIdx, 2);

    // Update currentIndex_ to point to next unprocessed event
    currentIndex_ = 0;
    while (currentIndex_ < events_.size() && events_[currentIndex_].time <= currentTime) {
        currentIndex_++;
    }
}

void BMSBgaManager::update(int64_t currentTime, double clockRate) {
    if (events_.empty()) return;

    // Detect time jump (seek or restart) - recalculate state from scratch
    // Skip detection on first update (lastUpdateTime_ == INT64_MIN)
    if (lastUpdateTime_ != INT64_MIN && currentTime < lastUpdateTime_ - 100) {
        std::cout << "BMSBgaManager: Time jump detected (" << lastUpdateTime_ << " -> " << currentTime << "), recalculating state" << std::endl;
        recalculateState(currentTime);
    }
    lastUpdateTime_ = currentTime;

    // Update active video players with relative time
    for (int i = 0; i < 3; i++) {
        if (layers_[i].active && layers_[i].mediaType == BgaMediaType::Video) {
            int bmpId = layers_[i].currentBmpId;
            if (videoCache_.count(bmpId) && videoCache_[bmpId]) {
                // Calculate relative time since video started, scaled by clockRate
                int64_t relativeTime = static_cast<int64_t>((currentTime - layers_[i].videoStartTime) * clockRate);
                if (relativeTime >= 0) {
                    videoCache_[bmpId]->update(relativeTime);
                    layers_[i].texture = videoCache_[bmpId]->getTexture();
                }
            }
        }
    }

    // Process all events before current time
    while (currentIndex_ < events_.size()) {
        const auto& evt = events_[currentIndex_];
        if (evt.time > currentTime) break;

        int layer = evt.layer;
        if (layer < 0 || layer >= 3) {
            currentIndex_++;
            continue;
        }

        // Debug: print event processing (every 100 events)
        if (currentIndex_ % 100 == 0) {
            std::cout << "BMSBgaManager: Processing event " << currentIndex_
                      << " time=" << evt.time << " layer=" << layer
                      << " bmpId=" << evt.bmpId << std::endl;
        }

        // Check media type and load if not cached
        if (mediaTypeCache_.find(evt.bmpId) == mediaTypeCache_.end()) {
            if (bmpDefs_.count(evt.bmpId)) {
                const std::string& filename = bmpDefs_[evt.bmpId];
                if (isVideoFile(filename)) {
                    mediaTypeCache_[evt.bmpId] = BgaMediaType::Video;
                    videoCache_[evt.bmpId] = loadVideo(filename);
                } else {
                    mediaTypeCache_[evt.bmpId] = BgaMediaType::Image;
                    // Layer 1 uses color key (black = transparent)
                    bool useColorKey = (layer == 1);
                    textureCache_[evt.bmpId] = loadTextureWithColorKey(filename, useColorKey);
                }
            } else {
                mediaTypeCache_[evt.bmpId] = BgaMediaType::Image;
                textureCache_[evt.bmpId] = nullptr;
            }
        }

        // Update layer state
        layers_[layer].active = true;
        layers_[layer].currentBmpId = evt.bmpId;
        layers_[layer].mediaType = mediaTypeCache_[evt.bmpId];

        if (layers_[layer].mediaType == BgaMediaType::Video) {
            if (videoCache_.count(evt.bmpId) && videoCache_[evt.bmpId]) {
                videoCache_[evt.bmpId]->reset();
                layers_[layer].videoStartTime = evt.time;  // Record video start time
                layers_[layer].texture = videoCache_[evt.bmpId]->getTexture();
            }
        } else {
            layers_[layer].texture = textureCache_[evt.bmpId];
        }

        currentIndex_++;
    }
}

void BMSBgaManager::render(int x, int y, int width, int height) {
    if (!renderer_) return;

    // Debug: print both layer states
    static int debugCounter = 0;
    if (debugCounter++ % 60 == 0) {
        std::cout << "BMSBgaManager::render - Layer0: active=" << layers_[0].active
                  << " bmpId=" << layers_[0].currentBmpId
                  << " texture=" << (layers_[0].texture ? "yes" : "no")
                  << " | Layer1: active=" << layers_[1].active
                  << " bmpId=" << layers_[1].currentBmpId
                  << " texture=" << (layers_[1].texture ? "yes" : "no") << std::endl;
    }

    // Check if miss layer should be shown
    int64_t currentTime = lastUpdateTime_;
    bool showMissLayer = (missLayerStartTime_ > 0 &&
                          currentTime >= missLayerStartTime_ &&
                          currentTime < missLayerStartTime_ + missLayerDuration_ &&
                          layers_[2].active && layers_[2].texture);

    if (showMissLayer) {
        // Draw miss layer (Poor BGA) instead of normal BGA
        SDL_FRect dstRect = {(float)x, (float)y, (float)width, (float)height};
        SDL_RenderTexture(renderer_, layers_[2].texture, nullptr, &dstRect);
    } else {
        // Render layers in order: BGA base (0), then Layer (1) with transparency
        for (int i = 0; i < 2; i++) {
            if (layers_[i].active && layers_[i].texture) {
                SDL_FRect dstRect = {(float)x, (float)y, (float)width, (float)height};

                // For video, maintain aspect ratio
                if (layers_[i].mediaType == BgaMediaType::Video) {
                    int bmpId = layers_[i].currentBmpId;
                    if (videoCache_.count(bmpId) && videoCache_[bmpId]) {
                        int videoW = videoCache_[bmpId]->getWidth();
                        int videoH = videoCache_[bmpId]->getHeight();
                        if (videoW > 0 && videoH > 0) {
                            float videoAspect = (float)videoW / videoH;
                            float targetAspect = (float)width / height;

                            if (videoAspect > targetAspect) {
                                float newHeight = width / videoAspect;
                                dstRect.y = y + (height - newHeight) / 2;
                                dstRect.h = newHeight;
                            } else {
                                float newWidth = height * videoAspect;
                                dstRect.x = x + (width - newWidth) / 2;
                                dstRect.w = newWidth;
                            }
                        }
                    }
                }

                SDL_RenderTexture(renderer_, layers_[i].texture, nullptr, &dstRect);
            }
        }
    }
}

void BMSBgaManager::triggerMissLayer(int64_t time) {
    // Only trigger if we have a poor layer defined
    if (layers_[2].active || layers_[2].currentBmpId >= 0) {
        missLayerStartTime_ = time;
    }
}

void BMSBgaManager::clear() {
    // Release all textures
    for (auto& [id, tex] : textureCache_) {
        if (tex) {
            SDL_DestroyTexture(tex);
        }
    }
    textureCache_.clear();

    // Release all video players
    for (auto& [id, player] : videoCache_) {
        if (player) {
            delete player;
        }
    }
    videoCache_.clear();
    mediaTypeCache_.clear();

    events_.clear();
    bmpDefs_.clear();
    currentIndex_ = 0;
    lastUpdateTime_ = INT64_MIN;
    missLayerStartTime_ = 0;

    for (int i = 0; i < 3; i++) {
        layers_[i].active = false;
        layers_[i].currentBmpId = -1;
        layers_[i].texture = nullptr;
        layers_[i].mediaType = BgaMediaType::Image;
    }
}
