#pragma once
#include <vector>
#include <string>
#include "Note.h"
#include "OsuParser.h"
#include "Renderer.h"
#include "AudioManager.h"
#include "Settings.h"
#include "ReplayParser.h"
#include "SkinManager.h"
#include "HPManager.h"
#include "KeySoundManager.h"
#include "Storyboard.h"

// Debug log entry for replay analysis
struct DebugLogEntry {
    int64_t time;           // Game time when event occurred
    std::string eventType;  // "KEY_DOWN", "KEY_UP", "JUDGEMENT", "TICK", "COMBO", etc.
    int lane;               // Lane number (-1 if not applicable)
    std::string details;    // Additional details
};

enum class GameState {
    Menu,
    SongSelect,
    Settings,
    KeyBinding,
    Playing,
    Paused,
    Dead,
    Result
};

// Beatmap source type
enum class BeatmapSource {
    Osu,
    DJMax,
    O2Jam
};

// Song entry for song select screen
struct SongEntry {
    std::string folderPath;      // Full path to song folder
    std::string folderName;      // Folder name (for display)
    std::string title;           // Song title (from beatmap)
    std::string artist;          // Artist name (from beatmap)
    std::string backgroundPath;  // Path to background image
    std::string audioPath;       // Path to audio file
    int previewTime;             // Preview start time in ms
    std::vector<std::string> beatmapFiles;  // List of beatmap files
    BeatmapSource source;        // osu!, DJMAX, O2Jam
};

class Game {
public:
    Game();
    ~Game();

    bool init();
    void run();

private:
    bool loadBeatmap(const std::string& path);
    void resetGame();
    void saveConfig();
    void loadConfig();
    std::string openFileDialog();
    std::string openReplayDialog();
    std::string saveReplayDialog();
    std::string openSkinFolderDialog();
    void handleInput();
    void update();
    void render();
    Judgement checkJudgement(int lane, int64_t atTime = INT64_MIN);
    void onKeyRelease(int lane, int64_t atTime = INT64_MIN);
    Judgement getJudgement(int64_t diff);
    void processJudgement(Judgement j, int lane);
    double calculateAccuracy();
    void updateReplay();
    int64_t getCurrentGameTime() const;  // Helper to get current game time

    Renderer renderer;
    AudioManager audio;
    SkinManager skinManager;
    HPManager hpManager;
    KeySoundManager keySoundManager;
    Storyboard storyboard;
    BeatmapInfo beatmap;
    std::string beatmapPath;
    size_t currentStoryboardSample;  // Index for storyboard sample playback

    // Replay mode
    bool replayMode;
    ReplayInfo replayInfo;
    size_t currentReplayFrame;

    // Recording replay
    std::vector<ReplayFrame> recordedFrames;
    int lastRecordedKeyState;

    GameState state;
    bool running;
    bool musicStarted;
    bool hasBackgroundMusic;  // false for keysound-only maps
    bool autoPlay;
    double baseBPM;  // Base BPM from first timing point
    int64_t startTime;
    static const int64_t PREPARE_TIME = 2500;

    int combo;
    int maxCombo;
    int score;
    double scoreAccumulator;  // Floating point score accumulator for precision
    double bonus;      // For score calculation, 0-100
    int totalNotes;    // Total notes in beatmap
    int judgementCounts[6];

    std::string lastJudgementText;
    int lastJudgementIndex;  // 0=300g, 1=300, 2=200, 3=100, 4=50, 5=miss
    int64_t lastJudgementTime;
    std::vector<HitError> hitErrors;

    // Combo animation
    int64_t lastComboChangeTime;  // Time when combo last increased
    bool comboBreak;              // Whether combo just broke
    int64_t comboBreakTime;       // Time when combo broke
    int lastComboValue;           // Combo value before break (for break animation)

    int fps;
    int frameCount;
    int64_t lastFpsTime;
    int64_t totalTime;
    int64_t lastFrameTime;
    int targetFrameDelay;

    int64_t judgeMarvelous;
    int64_t judgePerfect;
    int64_t judgeGreat;
    int64_t judgeGood;
    int64_t judgeBad;
    int64_t judgeMiss;  // miss window (188 - 3 * OD)

    int mouseX, mouseY;
    bool mouseClicked;
    bool mouseDown;
    bool laneKeyDown[10];  // track key state for each lane (up to 10k)

    Settings settings;
    SettingsCategory settingsCategory;
    int keyBindingIndex;
    int editingValue;
    bool editingVolume;
    bool dropdownExpanded;
    bool judgeModeDropdownExpanded;
    bool resolutionDropdownExpanded;
    bool refreshRateDropdownExpanded;
    bool keyCountDropdownExpanded;  // for key count selection
    int judgeDetailPopup;  // -1=none, 0-5=which judgement detail is open
    int pauseMenuSelection;  // 0=Resume, 1=Retry, 2=Exit
    int64_t pauseTime;  // time when paused
    int64_t allNotesFinishedTime;  // time when all notes were processed
    bool showEndPrompt;  // show "Press Enter to finish" prompt

    // Death state
    int deathMenuSelection;  // 0=Export Replay, 1=Retry, 2=Quit
    int64_t deathTime;       // time when death occurred
    float deathSlowdown;     // slowdown factor (1.0 -> 0.0)

    // Skip functionality
    int64_t skipTargetTime;  // Time to skip to (first note - 2000ms)
    bool canSkip;            // Whether skip is available

    bool editingUsername;  // true when editing username text input
    bool editingScrollSpeed;  // true when editing scroll speed input
    std::string scrollSpeedInput;  // temporary input string for scroll speed

    // Song select
    std::vector<SongEntry> songList;
    int selectedSongIndex;
    int selectedDifficultyIndex;  // Selected difficulty within the song
    float songSelectScroll;  // Scroll offset for song list
    bool songSelectNeedAutoScroll;  // Flag to trigger auto-scroll on selection change
    SDL_Texture* currentBgTexture;  // Background texture for selected song
    bool songSelectTransition;  // True when transitioning out
    int64_t songSelectTransitionStart;  // Transition start time
    void scanSongsFolder();
    void loadSongBackground(int index);
    void playPreviewMusic(int index);
    void stopPreviewMusic();
    void updatePreviewFade();  // Update fade in/out

    // Preview fade
    bool previewFading;
    bool previewFadeIn;  // true = fade in, false = fade out
    int64_t previewFadeStart;
    int previewFadeDuration;
    int previewTargetIndex;  // Song to play after fade out

    // Debug logging
    std::vector<DebugLogEntry> debugLog;
    void addDebugLog(int64_t time, const std::string& eventType, int lane, const std::string& details);
    void exportDebugLog();
};
