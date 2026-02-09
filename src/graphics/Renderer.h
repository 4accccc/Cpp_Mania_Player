#pragma once
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <vector>
#include <string>
#include "Note.h"
#include "Settings.h"
#include "OsuParser.h"
#include "ReplayAnalyzer.h"

class SkinManager;

struct HitError {
    int64_t time;
    int64_t offset;
};

class Renderer {
public:
    static const int NOTE_HEIGHT = 30;

    Renderer();
    ~Renderer();

    bool init();
    void clear();
    void present();
    void renderLanes();
    void renderStageBottom();
    void renderStageBorders();
    void renderKeys(const bool* laneKeyDown, int keyCount);
    void renderHitLighting(const bool* laneKeyDown, int keyCount);
    void renderNoteLighting(const bool* laneHoldActive, int keyCount, int64_t currentTime);  // LightingN/LightingL effects
    void triggerLightingN(int lane, int64_t time);  // Trigger LightingN animation on note hit
    void renderLaneHighlights(const bool* laneKeyDown, int keyCount, bool hiddenMod, bool fadeInMod, int combo);
    void renderNotes(std::vector<Note>& notes, int64_t currentTime, int scrollSpeed, double baseBPM, bool bpmScaleMode, const std::vector<TimingPoint>& timingPoints, const NoteColor* colors, bool hiddenMod = false, bool fadeInMod = false, int combo = 0, bool ignoreSV = false, double clockRate = 1.0);
    void renderJudgeLine();
    void renderJudgement(const std::string& text);
    void renderHitJudgement(int judgement, int64_t elapsedMs);  // elapsed time since judgement
    void renderSpeedInfo(int scrollSpeed, bool bpmScaleMode, bool autoPlay, bool autoPlayEnabled);
    void renderFPS(int fps);
    void renderGameInfo(int64_t currentTime, int64_t totalTime, const int* judgeCounts, double accuracy, int score);
    void renderCombo(int combo, int64_t comboAnimTime, bool comboBreak, int64_t breakAnimTime, int lastComboValue, bool holdActive, int64_t holdColorTime);
    void renderHPBar(double hpPercent);
    void renderHPBarKi(double currentHP, float barX, float barY, float scale);
    void renderScorePanel(const char* playerName, int score, double accuracy, int maxCombo);
    void renderHitErrorBar(const std::vector<HitError>& errors, int64_t currentTime,
                           int64_t window300g, int64_t window300, int64_t window200,
                           int64_t window100, int64_t window50, int64_t windowMiss, float scale);
    void renderResult(const std::string& title, const std::string& creator,
                      const int* judgeCounts, double accuracy, int maxCombo, int score);
    void renderMenu();
    bool renderButton(const char* text, float x, float y, float w, float h, int mouseX, int mouseY, bool clicked);

    // Settings UI components
    void renderSettingsWindow(int mouseX, int mouseY);
    bool renderCheckbox(const char* label, bool checked, float x, float y, int mouseX, int mouseY, bool clicked);
    int renderSliderWithValue(float x, float y, float w, int value, int minVal, int maxVal, int mouseX, int mouseY, bool mouseDown);
    int renderSliderWithFloatValue(float x, float y, float w, int value, int minVal, int maxVal, float divisor, int mouseX, int mouseY, bool mouseDown);
    void renderCategoryButton(const char* text, float x, float y, float w, float h, bool selected);
    void renderKeyBindingUI(SDL_Keycode* keys, int keyCount, int currentIndex);
    void renderText(const char* text, float x, float y);
    void renderTextClipped(const char* text, float x, float y, float maxWidth);
    void renderLabel(const char* text, float x, float y);
    int renderDropdown(const char* label, const char** options, int optionCount, int selected, float x, float y, float w, int mouseX, int mouseY, bool clicked, bool& expanded);
    bool renderRadioButton(const char* label, bool selected, float x, float y, int mouseX, int mouseY, bool clicked);
    bool renderClickableLabel(const char* text, float x, float y, int mouseX, int mouseY, bool clicked);
    void renderPopupWindow(float x, float y, float w, float h);
    void renderPauseMenu(int selection, float alpha = 1.0f);
    void renderDeathMenu(int selection, float slowdown, float alpha = 1.0f);
    void renderSkipPrompt();  // "Press Space to skip..." prompt
    bool renderTextInput(const char* label, std::string& text, float x, float y, float w, int mouseX, int mouseY, bool clicked, bool& editing, int& cursorPos);

    // Color box for settings
    bool renderColorBox(NoteColor color, float x, float y, float size, int mouseX, int mouseY, bool clicked);
    SDL_Color getNoteSDLColor(NoteColor color);

    SDL_Window* getWindow() { return window; }
    SDL_Renderer* getRenderer() { return renderer; }

    // Analysis chart rendering
    void renderAnalysisChart(const AnalysisResult& result, int chartType, float x, float y, float w, float h);
    bool saveAnalysisChart(const AnalysisResult& result, int chartType, const std::string& path, int width, int height);

    void setResolution(int width, int height);
    void setVSync(bool enabled);
    void setBorderlessFullscreen(bool enabled);
    void setWindowTitle(const std::string& title);
    void setKeyCount(int count);
    int getKeyCount() const { return keyCount; }
    void resetHitErrorIndicator();
    void setSkinManager(SkinManager* skin) { skinManager = skin; }

private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    TTF_Font* font;
    SkinManager* skinManager = nullptr;
    int laneStartX;
    int windowWidth;
    int windowHeight;
    int judgeLineY;
    int keyCount;
    int laneWidth;

    // Hit error indicator animation (osu! style 800ms tween)
    float hitErrorIndicatorPos;    // Current display position
    float hitErrorTargetPos;       // Target position (after 0.8/0.2 smoothing)
    float hitErrorAnimStartPos;    // Animation start position
    int64_t hitErrorAnimStartTime; // Animation start time
    size_t lastHitErrorCount;      // Track hit count to detect new hits

    // Skin-based layout (calculated from ManiaConfig)
    std::vector<float> columnWidths;   // Width of each column
    std::vector<float> columnX;        // X position of each column
    float stageStartX;                 // Stage left edge X
    float stageWidth;                  // Total stage width
    float skinHitPosition;             // Hit position from skin (default 402)

    // HP bar animation
    int hpBarCurrentFrame;             // Current animation frame
    int64_t hpBarLastFrameTime;        // Last frame switch time
    int hpBarFrameCount;               // Total frame count (cached)

    // LightingN animation (per lane hit time)
    int64_t lightingNHitTime[18];      // Time when note was hit (for LightingN animation)

    float getLaneX(int lane) const;
    float getLaneWidth(int lane) const;
    float getColumnSpacing(int lane) const;
    int getNoteY(int64_t noteTime, int64_t currentTime, int scrollSpeed, double baseBPM, bool bpmScaleMode, const std::vector<TimingPoint>& timingPoints, bool ignoreSV = false, double clockRate = 1.0) const;
    int getHoldHeadY(const Note& note, int naturalY, int64_t currentTime, int scrollSpeed) const;
    double getSVMultiplier(int64_t time, const std::vector<TimingPoint>& timingPoints) const;
    double getBaseBeatLength(int64_t time, const std::vector<TimingPoint>& timingPoints) const;
    void updateLaneLayout();
    void updateSkinLayout();
    float getElementScale() const;  // Scale for judgement and combo elements
};
