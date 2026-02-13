#pragma once
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <map>

// osu!mania specific config - per key count configuration
struct ManiaConfig {
    // Conversion factor from legacy positioning values (based in 480 dimensions) to 768
    static constexpr float POSITION_SCALE_FACTOR = 1.6f;
    // Size of a legacy column in the default skin (50 in 480 coords = 80 in 768 coords)
    static constexpr float DEFAULT_COLUMN_SIZE = 50.0f * POSITION_SCALE_FACTOR;  // 80
    // Default hit position (480 - 402) * 1.6 = 124.8
    static constexpr float DEFAULT_HIT_POSITION = (480.0f - 402.0f) * POSITION_SCALE_FACTOR;

    int keys = 4;                              // Key count (1-18)

    // Column configuration (values are scaled by POSITION_SCALE_FACTOR)
    std::vector<float> columnWidth;            // Column width (default 48 after scaling)
    std::vector<float> columnLineWidth;        // Column line width (default 2, NOT scaled)
    std::vector<bool> columnLine;              // Column line enable/disable
    std::vector<float> columnSpacing;          // Column spacing (default 0, scaled)
    float columnStart = 136.0f;                // Column start X position
    float columnRight = 19.0f;                 // Column right margin

    // Judgement and lighting config (positions are scaled)
    bool judgementLine = true;                 // Show judgement line
    float hitPosition = DEFAULT_HIT_POSITION;  // Hit position Y (scaled from 480 coord)
    float lightPosition = (480.0f - 413.0f) * POSITION_SCALE_FACTOR;  // Light position Y (scaled)
    int lightFramePerSecond = 60;              // Light frame rate (min 24)
    std::vector<float> explosionWidth;         // LightingN width per column
    std::vector<float> holdNoteLightWidth;     // LightingL width per column

    // Display position config (scaled)
    float comboPosition = 111.0f * POSITION_SCALE_FACTOR;   // Combo display Y position
    float scorePosition = 300.0f * POSITION_SCALE_FACTOR;   // Score display Y position
    float barlineHeight = 1.0f;                // Barline height (NOT scaled)
    float barlineWidth = 0.0f;                 // Barline width

    // Layout config
    int specialStyle = 0;                      // Special style (0=Normal, 1=Left, 2=Right)
    bool specialPositionLeft = false;          // Special position on left
    bool separateScore = true;                 // Separate score display
    bool keysUnderNotes = false;               // Keys under notes
    float stageSeparation = 40.0f;             // Stage separation (min 5)
    float widthForNoteHeightScale = 0.0f;      // Note height scale width (scaled)
    bool upsideDown = false;                   // Upside down display

    // Note body style (global, can be overridden per column)
    int noteBodyStyle = -1;                    // -1 = not set, use version default

    // Note image config (per column)
    std::vector<std::string> noteImage;        // Normal note image
    std::vector<std::string> noteImageH;       // Hold note head
    std::vector<std::string> noteImageT;       // Hold note tail
    std::vector<std::string> noteImageL;       // Hold note body
    std::vector<bool> noteFlipWhenUpsideDown;  // Flip when upside down
    std::vector<int> noteBodyStylePerColumn;   // Note body style per column (-1 = use global)

    // Key image config (per column)
    std::vector<std::string> keyImage;         // Key image
    std::vector<std::string> keyImageD;        // Key pressed image

    // Stage image config
    std::string stageLeft;                     // Left border
    std::string stageRight;                    // Right border
    std::string stageBottom;                   // Stage bottom
    std::string stageHint;                     // Stage hint / judgement line image
    std::string stageLight;                    // Stage light
    std::string lightingN;                     // Normal lighting
    std::string lightingL;                     // Hold lighting
    std::string warningArrow;                  // Warning arrow

    // Image lookups dictionary (stores custom image paths from skin.ini)
    // Keys: NoteImage0, NoteImage0H, KeyImage0, KeyImage0D, Hit300g, etc.
    std::map<std::string, std::string> imageLookups;

    // Color config
    std::vector<SDL_Color> colour;             // Column colors
    SDL_Color colourColumnLine = {255,255,255,255};   // Column line color
    SDL_Color colourJudgementLine = {255,255,255,255};// Judgement line color
    SDL_Color colourBarline = {255,255,255,255};      // Barline color
    SDL_Color colourBreak = {255,0,0,255};            // Combo break color
    SDL_Color colourHold = {255,191,51,255};          // Hold color
    SDL_Color colourKeyWarning = {255,0,0,255};       // Key warning color
    std::vector<SDL_Color> colourLight;        // Light colors

    // Combo font config
    std::string fontCombo;                     // Combo font prefix
    int comboOverlap = 0;                      // Combo digit overlap

    ManiaConfig() {
        columnWidth.resize(keys, DEFAULT_COLUMN_SIZE);
        columnLineWidth.resize(keys + 1, 2.0f);
        columnLine.resize(keys + 1, true);
        columnSpacing.resize(keys - 1, 0.0f);
        explosionWidth.resize(keys, 0.0f);
        holdNoteLightWidth.resize(keys, 0.0f);
    }

    // Initialize arrays for specific key count
    void initForKeys(int keyCount) {
        keys = keyCount;
        columnWidth.assign(keys, DEFAULT_COLUMN_SIZE);
        columnLineWidth.assign(keys + 1, 0.0f);  // Default: no separator lines
        columnLine.assign(keys + 1, true);
        columnSpacing.assign(keys > 1 ? keys - 1 : 0, 0.0f);
        explosionWidth.assign(keys, 0.0f);
        holdNoteLightWidth.assign(keys, 0.0f);
    }

    // Get minimum column width
    float getMinimumColumnWidth() const {
        if (columnWidth.empty()) return DEFAULT_COLUMN_SIZE;
        float minWidth = columnWidth[0];
        for (size_t i = 1; i < columnWidth.size(); i++) {
            if (columnWidth[i] < minWidth) minWidth = columnWidth[i];
        }
        return minWidth;
    }
};

// Main skin config - corresponds to skin.ini [General], [Colours], [Fonts] sections
struct SkinConfig {
    // [General] section
    std::string name;
    std::string author;
    std::string version = "latest";
    int animationFramerate = 0;  // 0 = use default (1000/frameCount or 60fps)

    // [Fonts] section
    std::string hitCirclePrefix = "default";
    int hitCircleOverlap = -2;
    std::string scorePrefix = "score";
    std::string comboPrefix = "score";
    int scoreOverlap = 0;
    int comboOverlap = 0;

    // [Colours] section
    std::vector<SDL_Color> comboColours;       // Combo1-5
    SDL_Color menuGlow = {255,255,255,255};
    SDL_Color sliderBorder = {255,255,255,255};

    // osu!mania config - indexed by key count
    std::map<int, ManiaConfig> maniaConfigs;

    // Get mania config for specified key count
    const ManiaConfig* getManiaConfig(int keyCount) const {
        auto it = maniaConfigs.find(keyCount);
        return it != maniaConfigs.end() ? &it->second : nullptr;
    }

    ManiaConfig* getOrCreateManiaConfig(int keyCount) {
        if (maniaConfigs.find(keyCount) == maniaConfigs.end()) {
            maniaConfigs[keyCount] = ManiaConfig();
            maniaConfigs[keyCount].initForKeys(keyCount);
        }
        return &maniaConfigs[keyCount];
    }
};
