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

private:
    SDL_Texture* loadTexture(const std::string& filename);
    VideoPlayer* loadVideo(const std::string& filename);
    bool isVideoFile(const std::string& filename);

    SDL_Renderer* renderer_ = nullptr;
    std::string directory_;
    std::vector<BMSBgaEvent> events_;
    std::unordered_map<int, std::string> bmpDefs_;
    std::unordered_map<int, SDL_Texture*> textureCache_;
    std::unordered_map<int, VideoPlayer*> videoCache_;
    std::unordered_map<int, BgaMediaType> mediaTypeCache_;
    size_t currentIndex_ = 0;

    // Three layers: BGA base, Layer, Poor
    BMSBgaLayer layers_[3];
};
