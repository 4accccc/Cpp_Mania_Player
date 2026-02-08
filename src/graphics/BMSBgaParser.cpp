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

    // Reset all layers
    for (int i = 0; i < 3; i++) {
        layers_[i].active = false;
        layers_[i].currentBmpId = -1;
        layers_[i].texture = nullptr;
    }

    std::cout << "BMSBgaManager: Loaded " << events_.size() << " events, "
              << bmpDefs_.size() << " BMP definitions" << std::endl;
}

SDL_Texture* BMSBgaManager::loadTexture(const std::string& filename) {
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
        return nullptr;
    }

    int w, h, channels;
    unsigned char* data = stbi_load(imgPath.string().c_str(), &w, &h, &channels, 4);
    if (!data) {
        return nullptr;
    }

    SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, data, w * 4);
    SDL_Texture* texture = nullptr;
    if (surface) {
        texture = SDL_CreateTextureFromSurface(renderer_, surface);
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

void BMSBgaManager::update(int64_t currentTime, double clockRate) {
    if (events_.empty()) return;

    // Update active video players with relative time
    for (int i = 0; i < 2; i++) {
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

        // Check media type and load if not cached
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

    // Render layers in order: BGA base (0), Layer (1)
    // Poor layer (2) only shown on miss, not handled here
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
                            // Video is wider, fit to width
                            float newHeight = width / videoAspect;
                            dstRect.y = y + (height - newHeight) / 2;
                            dstRect.h = newHeight;
                        } else {
                            // Video is taller, fit to height
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

    for (int i = 0; i < 3; i++) {
        layers_[i].active = false;
        layers_[i].currentBmpId = -1;
        layers_[i].texture = nullptr;
        layers_[i].mediaType = BgaMediaType::Image;
    }
}
