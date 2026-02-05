#pragma once
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <map>

// osu!mania specific config - per key count configuration
struct ManiaConfig {
    int keys = 4;                              // Key count (1-18)

    // Column configuration
    std::vector<float> columnWidth;            // Column width (default 30, range 5-100)
    std::vector<float> columnLineWidth;        // Column line width (default 2, min 2)
    std::vector<bool> columnLine;              // Column line enable/disable
    std::vector<float> columnSpacing;          // Column spacing (default 0)
    float columnStart = 136.0f;                // Column start X position
    float columnRight = 19.0f;                 // Column right margin

    // Judgement and lighting config
    bool judgementLine = true;                 // Show judgement line
    int hitPosition = 402;                     // Hit position Y (range 240-480, default 402)
    int lightPosition = 413;                   // Light position Y
    int lightFramePerSecond = 60;              // Light frame rate (min 24)
    std::vector<float> lightingNWidth;         // Normal note lighting width
    std::vector<float> lightingLWidth;         // Hold note lighting width

    // Display position config
    int comboPosition = 111;                   // Combo display Y position
    int scorePosition = 325;                   // Score display Y position
    float barlineHeight = 1.2f;                // Barline height
    float barlineWidth = 0.0f;                 // Barline width

    // Layout config
    int specialStyle = 0;                      // Special style (0=Normal, 1=Left, 2=Right)
    bool specialPositionLeft = false;          // Special position on left
    bool separateScore = true;                 // Separate score display
    bool keysUnderNotes = false;               // Keys under notes
    float stageSeparation = 40.0f;             // Stage separation (min 5)
    float widthForNoteHeightScale = 0.0f;      // Note height scale width
    bool upsideDown = false;                   // Upside down display

    // Note image config (per column)
    std::vector<std::string> noteImage;        // Normal note image
    std::vector<std::string> noteImageH;       // Hold note head
    std::vector<std::string> noteImageT;       // Hold note tail
    std::vector<std::string> noteImageL;       // Hold note body
    std::vector<bool> noteFlipWhenUpsideDown;  // Flip when upside down
    std::vector<int> noteBodyStyle;            // Note body style

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
        columnWidth.resize(keys, 30.0f);
        columnLineWidth.resize(keys + 1, 2.0f);
        columnLine.resize(keys + 1, true);
        columnSpacing.resize(keys - 1, 0.0f);
    }
};

// Main skin config - corresponds to skin.ini [General], [Colours], [Fonts] sections
struct SkinConfig {
    // [General] section
    std::string name;
    std::string author;
    std::string version = "latest";

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
            maniaConfigs[keyCount].keys = keyCount;
        }
        return &maniaConfigs[keyCount];
    }
};
