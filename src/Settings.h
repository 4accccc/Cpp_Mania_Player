#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <string>
#include <vector>

struct JudgementConfig {
    int64_t window;
    bool breaksCombo;
    double accuracy;
    bool enabled;
};

enum class JudgementMode {
    BeatmapOD,
    CustomOD,
    CustomWindows
};

enum class SettingsCategory {
    Sound,
    Graphics,
    Input,
    Judgement,
    Modifiers,
    Misc
};

// Note colors: 0=White, 1=Blue, 2=Yellow, 3=Pink
enum class NoteColor {
    White = 0,
    Blue = 1,
    Yellow = 2,
    Pink = 3
};

struct Settings {
    int volume;
    int audioDevice;
    int audioOffset;  // Audio offset in ms (negative = notes earlier, positive = notes later)
    int quality;
    bool lowSpecMode;
    int resolution;
    int refreshRate;
    bool vsync;
    bool borderlessFullscreen;
    bool laneLight;  // Lane highlight effect

    // Key settings for 1-10k
    int selectedKeyCount;  // Currently selected key count in settings (1-10)
    SDL_Keycode keys[10][10];  // Keys for each key count [keyCount-1][lane]
    NoteColor laneColors[10];  // Colors for each lane
    bool n1Style;  // 8k N+1 style
    bool mirror;   // 8k N+1 mirror

    JudgementMode judgeMode;
    float customOD;
    bool funMode;
    bool noteLock;
    bool legacyHoldJudgement;
    JudgementConfig judgements[6];
    float hitErrorBarScale;  // Hit error bar size (0.5x - 3.0x)

    // Username settings
    std::string username;
    bool forceOverrideUsername;  // Force override player name when exporting replay

    // Modifiers
    bool autoPlayEnabled;  // AutoPlay mod enabled in settings
    bool hiddenEnabled;    // Hidden mod enabled in settings
    bool fadeInEnabled;    // FadeIn mod enabled in settings
    bool ignoreSV;         // Ignore scroll velocity changes
    bool deathEnabled;     // Death mod - HP=0 causes death
    bool suddenDeathEnabled; // Sudden Death mod - any miss causes death

    // Scroll speed settings (osu!mania style)
    int scrollSpeed;       // 1-40, default 24
    bool bpmScaleMode;     // true=BPM scale, false=fixed speed (default)

    // Skin settings
    std::string skinPath;
    bool ignoreBeatmapSkin;
    bool ignoreBeatmapHitsounds;
    bool disableStoryboard;

    // Debug settings
    bool debugEnabled;     // Enable debug logging for replay analysis

    Settings() {
        volume = 100;
        audioDevice = 0;
        audioOffset = 0;  // Default no offset
        quality = 2;
        lowSpecMode = false;
        resolution = 0;
        refreshRate = 4;  // 1000 FPS
        vsync = false;
        borderlessFullscreen = false;
        laneLight = true;  // Default on

        selectedKeyCount = 4;
        n1Style = true;
        mirror = true;  // Default to mirror mode for 8K

        // Initialize all keys for all key counts
        for (int k = 0; k < 10; k++) {
            for (int i = 0; i < 10; i++) {
                keys[k][i] = SDLK_UNKNOWN;
            }
            setDefaultKeys(k + 1);
        }

        // Initialize all colors and set defaults for 4K
        for (int i = 0; i < 10; i++) {
            laneColors[i] = NoteColor::White;
        }
        setDefaultColors(4);

        judgeMode = JudgementMode::BeatmapOD;
        customOD = 5.0f;
        funMode = false;
        noteLock = true;
        legacyHoldJudgement = true;
        hitErrorBarScale = 1.0f;

        username = "Guest";
        forceOverrideUsername = false;

        autoPlayEnabled = false;  // AutoPlay disabled by default
        hiddenEnabled = false;    // Hidden disabled by default
        fadeInEnabled = false;    // FadeIn disabled by default
        ignoreSV = false;         // Ignore SV disabled by default
        deathEnabled = false;     // Death mod disabled by default
        suddenDeathEnabled = false; // Sudden Death mod disabled by default

        scrollSpeed = 24;         // Default scroll speed (osu! default is around 24)
        bpmScaleMode = false;     // Default to fixed speed mode

        skinPath = "";
        ignoreBeatmapSkin = false;
        ignoreBeatmapHitsounds = false;
        disableStoryboard = false;

        debugEnabled = false;

        int64_t windows[] = {16, 64, 97, 127, 151, 188};
        double accs[] = {100, 100, 66.67, 33.33, 16.67, 0};
        bool breaks[] = {false, false, false, false, false, true};

        for (int i = 0; i < 6; i++) {
            judgements[i].window = windows[i];
            judgements[i].accuracy = accs[i];
            judgements[i].breaksCombo = breaks[i];
            judgements[i].enabled = true;
        }
    }

    // Set default colors for a given key count
    void setDefaultColors(int keyCount) {
        // Default colors based on key count
        // White=0, Blue=1, Yellow=2, Pink=3
        switch (keyCount) {
            case 1:
                laneColors[0] = NoteColor::Yellow;
                break;
            case 2:
                laneColors[0] = NoteColor::White;
                laneColors[1] = NoteColor::Blue;
                break;
            case 3:
                laneColors[0] = NoteColor::White;
                laneColors[1] = NoteColor::Blue;
                laneColors[2] = NoteColor::White;
                break;
            case 4:
                laneColors[0] = NoteColor::White;
                laneColors[1] = NoteColor::Blue;
                laneColors[2] = NoteColor::White;
                laneColors[3] = NoteColor::Blue;
                break;
            case 5:
                laneColors[0] = NoteColor::White;
                laneColors[1] = NoteColor::Blue;
                laneColors[2] = NoteColor::Yellow;
                laneColors[3] = NoteColor::Blue;
                laneColors[4] = NoteColor::White;
                break;
            case 6:
                laneColors[0] = NoteColor::White;
                laneColors[1] = NoteColor::Blue;
                laneColors[2] = NoteColor::White;
                laneColors[3] = NoteColor::White;
                laneColors[4] = NoteColor::Blue;
                laneColors[5] = NoteColor::White;
                break;
            case 7:
                laneColors[0] = NoteColor::White;
                laneColors[1] = NoteColor::Blue;
                laneColors[2] = NoteColor::White;
                laneColors[3] = NoteColor::Yellow;
                laneColors[4] = NoteColor::White;
                laneColors[5] = NoteColor::Blue;
                laneColors[6] = NoteColor::White;
                break;
            case 8:
                if (this->n1Style) {
                    if (this->mirror) {
                        // Mirror: 白蓝白黄白蓝白 粉红
                        laneColors[0] = NoteColor::White;
                        laneColors[1] = NoteColor::Blue;
                        laneColors[2] = NoteColor::White;
                        laneColors[3] = NoteColor::Yellow;
                        laneColors[4] = NoteColor::White;
                        laneColors[5] = NoteColor::Blue;
                        laneColors[6] = NoteColor::White;
                        laneColors[7] = NoteColor::Pink;
                    } else {
                        // N+1: 粉红 白蓝白黄白蓝白
                        laneColors[0] = NoteColor::Pink;
                        laneColors[1] = NoteColor::White;
                        laneColors[2] = NoteColor::Blue;
                        laneColors[3] = NoteColor::White;
                        laneColors[4] = NoteColor::Yellow;
                        laneColors[5] = NoteColor::White;
                        laneColors[6] = NoteColor::Blue;
                        laneColors[7] = NoteColor::White;
                    }
                } else {
                    // Normal 8k: 白蓝白蓝白蓝白蓝
                    laneColors[0] = NoteColor::White;
                    laneColors[1] = NoteColor::Blue;
                    laneColors[2] = NoteColor::White;
                    laneColors[3] = NoteColor::Blue;
                    laneColors[4] = NoteColor::White;
                    laneColors[5] = NoteColor::Blue;
                    laneColors[6] = NoteColor::White;
                    laneColors[7] = NoteColor::Blue;
                }
                break;
            case 9:
                // 白蓝白蓝黄白蓝白蓝
                laneColors[0] = NoteColor::White;
                laneColors[1] = NoteColor::Blue;
                laneColors[2] = NoteColor::White;
                laneColors[3] = NoteColor::Blue;
                laneColors[4] = NoteColor::Yellow;
                laneColors[5] = NoteColor::White;
                laneColors[6] = NoteColor::Blue;
                laneColors[7] = NoteColor::White;
                laneColors[8] = NoteColor::Blue;
                break;
            case 10:
                // 白蓝白蓝黄黄白蓝白蓝
                laneColors[0] = NoteColor::White;
                laneColors[1] = NoteColor::Blue;
                laneColors[2] = NoteColor::White;
                laneColors[3] = NoteColor::Blue;
                laneColors[4] = NoteColor::Yellow;
                laneColors[5] = NoteColor::Yellow;
                laneColors[6] = NoteColor::White;
                laneColors[7] = NoteColor::Blue;
                laneColors[8] = NoteColor::White;
                laneColors[9] = NoteColor::Blue;
                break;
        }
    }

    // Set default keys for a given key count (osu!mania official defaults)
    void setDefaultKeys(int keyCount) {
        int k = keyCount - 1;  // Array index
        switch (keyCount) {
            case 1:
                keys[k][0] = SDLK_SPACE;
                break;
            case 2:
                keys[k][0] = SDLK_F;
                keys[k][1] = SDLK_J;
                break;
            case 3:
                keys[k][0] = SDLK_F;
                keys[k][1] = SDLK_SPACE;
                keys[k][2] = SDLK_J;
                break;
            case 4:
                keys[k][0] = SDLK_D;
                keys[k][1] = SDLK_F;
                keys[k][2] = SDLK_J;
                keys[k][3] = SDLK_K;
                break;
            case 5:
                keys[k][0] = SDLK_D;
                keys[k][1] = SDLK_F;
                keys[k][2] = SDLK_SPACE;
                keys[k][3] = SDLK_J;
                keys[k][4] = SDLK_K;
                break;
            case 6:
                keys[k][0] = SDLK_S;
                keys[k][1] = SDLK_D;
                keys[k][2] = SDLK_F;
                keys[k][3] = SDLK_J;
                keys[k][4] = SDLK_K;
                keys[k][5] = SDLK_L;
                break;
            case 7:
                keys[k][0] = SDLK_S;
                keys[k][1] = SDLK_D;
                keys[k][2] = SDLK_F;
                keys[k][3] = SDLK_SPACE;
                keys[k][4] = SDLK_J;
                keys[k][5] = SDLK_K;
                keys[k][6] = SDLK_L;
                break;
            case 8:
                if (n1Style) {
                    if (mirror) {
                        // 8K(R): K3,K4,K5,K6,K7,K8,S1
                        keys[k][0] = SDLK_D;
                        keys[k][1] = SDLK_F;
                        keys[k][2] = SDLK_SPACE;
                        keys[k][3] = SDLK_J;
                        keys[k][4] = SDLK_K;
                        keys[k][5] = SDLK_L;
                        keys[k][6] = SDLK_LSHIFT;
                    } else {
                        // 8K(L): S1,K3,K4,K5,K6,K7,K8
                        keys[k][0] = SDLK_LSHIFT;
                        keys[k][1] = SDLK_D;
                        keys[k][2] = SDLK_F;
                        keys[k][3] = SDLK_SPACE;
                        keys[k][4] = SDLK_J;
                        keys[k][5] = SDLK_K;
                        keys[k][6] = SDLK_L;
                    }
                } else {
                    // Normal 8K: K1,K2,K3,K4,K6,K7,K8,K9
                    keys[k][0] = SDLK_A;
                    keys[k][1] = SDLK_S;
                    keys[k][2] = SDLK_D;
                    keys[k][3] = SDLK_F;
                    keys[k][4] = SDLK_J;
                    keys[k][5] = SDLK_K;
                    keys[k][6] = SDLK_L;
                    keys[k][7] = SDLK_SEMICOLON;
                }
                break;
            case 9:
                keys[k][0] = SDLK_A;
                keys[k][1] = SDLK_S;
                keys[k][2] = SDLK_D;
                keys[k][3] = SDLK_F;
                keys[k][4] = SDLK_SPACE;
                keys[k][5] = SDLK_J;
                keys[k][6] = SDLK_K;
                keys[k][7] = SDLK_L;
                keys[k][8] = SDLK_SEMICOLON;
                break;
            case 10:
                keys[k][0] = SDLK_A;
                keys[k][1] = SDLK_S;
                keys[k][2] = SDLK_D;
                keys[k][3] = SDLK_F;
                keys[k][4] = SDLK_SPACE;
                keys[k][5] = SDLK_N;
                keys[k][6] = SDLK_J;
                keys[k][7] = SDLK_K;
                keys[k][8] = SDLK_L;
                keys[k][9] = SDLK_SEMICOLON;
                break;
        }
    }
};
