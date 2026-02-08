#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

// VCQ entry - timeline event
struct VcqEntry {
    uint32_t frameTime;     // Frame number (30fps)
    uint32_t layerId;       // Layer ID for rendering order
    std::string vceFile;    // VCE filename to load
};

// VCE image resource
struct VceImageResource {
    uint32_t index;
    std::string filename;
};

// VCE keyframe data
struct VceKeyframe {
    uint32_t frame;
    uint16_t flags;             // +4: if non-zero, use delta mode
    int imageIndex;             // Which image this keyframe belongs to

    // Transform values (absolute or delta depending on flags)
    float values[18];           // +8 to +76: 18 floats
    float deltaValue;           // +80
    uint32_t interpType;        // +84: interpolation type (0-5)
    float interpSpeed;          // +88: interpolation speed
    float extraValues[5];       // +92 to +108
};

// VCE animation effect
struct VceEffect {
    uint32_t totalFrames;
    uint32_t frameRate;
    std::vector<VceImageResource> images;
    std::vector<VceKeyframe> keyframes;
};

// BGA layer state
struct BgaLayer {
    bool active;
    std::string currentVce;
    VceEffect effect;
    uint32_t startFrame;    // Frame when this VCE started
    int currentKfIndex;     // Current keyframe index (-1 = done)

    // Current transform state (accumulated values)
    float values[18];       // Transform values
    float frameIndex;       // Current frame index for animation
    int currentImageIndex;

    // Convenience accessors
    float& posX() { return values[0]; }
    float& posY() { return values[1]; }
    float& scaleX() { return values[4]; }
    float& scaleY() { return values[5]; }
    float& alpha() { return values[17]; }
};

// Complete BGA data
struct BgaData {
    std::vector<VcqEntry> timeline;
    std::unordered_map<int, BgaLayer> layers;  // layerId -> layer state
    std::string bgaDir;     // Directory containing BGA files
};

class BgaParser {
public:
    // Parse VCQ timeline file
    static bool parseVcq(const std::string& path, std::vector<VcqEntry>& entries);

    // Parse VCE effect file
    static bool parseVce(const std::string& path, VceEffect& effect);

    // Get VCQ filename for a song
    static std::string getVcqPath(const std::string& bgaDir, const std::string& songName);
};
