#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <SDL3/SDL.h>
#include "VideoPlayer.h"

// Forward declaration - BMSBgaEvent is defined in BMSParser.h
struct BMSBgaEvent;

// BGA media type
enum class BgaMediaType {
    Image,
    Video
};

// BMS BGA layer state
struct BMSBgaLayer {
    bool active = false;
    int currentBmpId = -1;
    SDL_Texture* texture = nullptr;
    BgaMediaType mediaType = BgaMediaType::Image;
    int64_t videoStartTime = 0;  // Game time when video started
};

// BMS BGA manager
class BMSBgaManager {
public:
    BMSBgaManager();
    ~BMSBgaManager();

    // Initialize
    void init(SDL_Renderer* renderer);

    // Load BGA data
    void load(const std::vector<BMSBgaEvent>& events,
              const std::unordered_map<int, std::string>& bmpDefs,
              const std::string& directory);

    // Update BGA state (clockRate for DT/HT mods)
    void update(int64_t currentTime, double clockRate = 1.0);

    // Render BGA
    void render(int x, int y, int width, int height);

    // Clear resources
    void clear();

    // Check if has BGA
    bool hasBga() const { return !events_.empty(); }

    // Trigger miss layer (Poor BGA) display
    void triggerMissLayer(int64_t time);

    // Set miss layer duration (default 500ms)
    void setMissLayerDuration(int64_t duration) { missLayerDuration_ = duration; }

private:
    SDL_Texture* loadTexture(const std::string& filename);
    SDL_Texture* loadTextureWithColorKey(const std::string& filename, bool useColorKey);
    VideoPlayer* loadVideo(const std::string& filename);
    bool isVideoFile(const std::string& filename);
    void recalculateState(int64_t currentTime);  // Recalculate BGA state from scratch

    SDL_Renderer* renderer_ = nullptr;
    std::string directory_;
    std::vector<BMSBgaEvent> events_;
    std::unordered_map<int, std::string> bmpDefs_;
    std::unordered_map<int, SDL_Texture*> textureCache_;
    std::unordered_map<int, VideoPlayer*> videoCache_;
    std::unordered_map<int, BgaMediaType> mediaTypeCache_;
    size_t currentIndex_ = 0;
    int64_t lastUpdateTime_ = INT64_MIN;  // Track last update time for seek detection

    // Three layers: BGA base, Layer, Poor
    BMSBgaLayer layers_[3];

    // Miss layer (Poor BGA) state
    int64_t missLayerStartTime_ = 0;      // When miss layer started showing
    int64_t missLayerDuration_ = 500;     // How long to show miss layer (ms)
};
