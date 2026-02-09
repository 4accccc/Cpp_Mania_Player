#pragma once
#include <vector>
#include <string>
#include "Note.h"
#include "OsuParser.h"
#include "BMSParser.h"
#include "BMSBgaParser.h"
#include "Renderer.h"
#include "AudioManager.h"
#include "Settings.h"
#include "ReplayParser.h"
#include "ReplayAnalyzer.h"
#include "SkinManager.h"
#include "HPManager.h"
#include "KeySoundManager.h"
#include "Storyboard.h"
#include "PPCalculator.h"
#include "DJMAXOLBgaParser.h"
#include "VideoGenerator.h"
#include "JudgementSystem.h"

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
    Result,
    ReplayFactory
};

// Beatmap source type
enum class BeatmapSource {
    Osu,
    DJMax,
    O2Jam,
    BMS,
    Malody
};

// Difficulty info for song select
struct DifficultyInfo {
    std::string path;       // Full path to beatmap file
    std::string version;    // Difficulty name
    std::string creator;    // Charter/mapper
    std::string hash;       // MD5 hash of beatmap file
    int keyCount;           // Number of keys (4K, 7K, etc.)
    // Star ratings for each algorithm version (matches STAR_RATING_VERSION_COUNT)
    // [0] = b20260101, [1] = b20220101
    double starRatings[2] = {0.0, 0.0};
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
    std::vector<std::string> beatmapFiles;  // List of beatmap files (legacy)
    std::vector<DifficultyInfo> difficulties;  // Detailed difficulty info
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
    void cleanupTempDir();  // Clean up temporary files after song ends
    void updateBga(int64_t currentTime);  // Update BGA state
    void renderBga();  // Render BGA layers
    void saveConfig();
    void loadConfig();
    std::string openFileDialog();
    std::string openReplayDialog();
    std::string saveReplayDialog();
    std::string saveImageDialog();
    std::string openSkinFolderDialog();
    void handleInput();
    void update();
    void render();
    Judgement checkJudgement(int lane, int64_t atTime = INT64_MIN);
    void onKeyRelease(int lane, int64_t atTime = INT64_MIN);
    Judgement getJudgement(int64_t diff, int64_t noteTime, int64_t currentTime);
    void processJudgement(Judgement j, int lane);
    double calculateAccuracy();
    void updateReplay();
    int64_t getCurrentGameTime() const;  // Helper to get current game time (with audio offset, for judgement)
    int64_t getRenderTime() const;        // Helper to get render time (without audio offset)

    Renderer renderer;
    AudioManager audio;
    SkinManager skinManager;
    HPManager hpManager;
    KeySoundManager keySoundManager;
    Storyboard storyboard;
    bool lastStoryboardPassing = true;  // Track passing state for triggers
    PPCalculator ppCalculator;

    // BGA (DJMAX Online background animation)
    BgaData bgaData;
    bool hasBga;
    size_t currentBgaEntry;  // Current index in VCQ timeline
    std::unordered_map<std::string, SDL_Texture*> bgaTextures;  // Cached BGA textures

    // BMS BGA
    BMSBgaManager bmsBgaManager;
    bool isBmsBga;  // true if current BGA is BMS format

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
    double clockRate;           // Speed mod: 1.0, 1.5 (DT/NC), or 0.75 (HT)
    double currentStarRating;   // Star rating with current mods applied
    double scoreMultiplier;     // Score multiplier: 0.5 for HT, 1.0 for others
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
    bool anyHoldActive;           // Whether any hold note is currently being held
    int64_t holdColorChangeTime;  // Time when hold state changed (for color transition)

    int fps;
    int frameCount;
    int64_t lastFpsTime;
    int64_t totalTime;
    int64_t lastFrameTime;
    int targetFrameDelay;

    // Performance monitoring
    double perfInput;   // Input handling time (ms)
    double perfUpdate;  // Update time (ms)
    double perfDraw;    // Draw time (ms)
    double perfAudio;   // Audio time (ms)

    JudgementSystem judgementSystem;

    int mouseX, mouseY;
    bool mouseClicked;
    bool mouseDown;
    bool laneKeyDown[10];  // track key state for each lane (up to 10k)
    int laneNextNoteIndex[10];  // index of next note in each lane (for empty tap keysound)

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
    bool starRatingDropdownExpanded;  // for star rating version selection
    float settingsScroll;  // Scroll offset for settings panel
    bool settingsDragging;  // Mouse drag scrolling for settings
    int settingsDragStartY;
    float settingsDragStartScroll;
    int judgeDetailPopup;  // -1=none, 0-5=which judgement detail is open
    int pauseMenuSelection;  // 0=Resume, 1=Retry, 2=Exit
    int64_t pauseTime;  // time when paused
    int64_t pauseGameTime;  // game time when paused (for rendering)
    bool pauseFadingOut;  // true when fading out from pause
    int64_t pauseFadeOutStart;  // time when fade out started
    int64_t pauseAudioOffset;  // offset to correct audio position after resume
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
    int settingsCursorPos = 0;  // Cursor position for settings text input

    // Song select
    std::vector<SongEntry> songList;
    int selectedSongIndex;
    int selectedDifficultyIndex;  // Selected difficulty within the song
    float songSelectScroll;  // Scroll offset for song list
    bool songSelectNeedAutoScroll;  // Flag to trigger auto-scroll on selection change
    bool songSelectDragging;  // Mouse drag scrolling
    int songSelectDragStartY;  // Mouse Y when drag started
    float songSelectDragStartScroll;  // Scroll value when drag started
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

    // Replay Factory
    std::string factoryReplayPath;  // Loaded replay file path
    ReplayInfo factoryReplayInfo;   // Loaded replay data
    ReplayInfo factoryReplayInfoMirrored;  // Mirrored replay data
    bool factoryMirrorInput = false; // Mirror input checkbox state

    // Replay Factory editing states
    bool editingPlayerName = false;
    bool editingTimestamp = false;
    bool editingJudgements = false;
    bool editingScore = false;
    bool editingCombo = false;
    int cursorPos = 0;  // Cursor position for text input
    std::string factoryTimestampStr;
    std::string factoryJudgementsStr;
    std::string factoryScoreStr;
    std::string factoryComboStr;

    // Replay Analysis
    bool showAnalysisWindow = false;
    int analysisWindowType = 0;  // 0 = press distribution, 1 = realtime press
    AnalysisResult analysisResult;
    SDL_Texture* analysisTexture = nullptr;

    // Video Generation
    VideoGenerator videoGenerator;
    bool editingBlockHeight = false;
    std::string blockHeightInput = "40";
    bool editingVideoWidth = false;
    std::string videoWidthInput = "540";
    bool editingVideoHeight = false;
    std::string videoHeightInput = "960";
    bool editingVideoFPS = false;
    std::string videoFPSInput = "60";
    bool videoShowHolding = false;

    // Debug logging
    std::vector<DebugLogEntry> debugLog;
    void addDebugLog(int64_t time, const std::string& eventType, int lane, const std::string& details);
    void exportDebugLog();
};
