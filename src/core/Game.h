#pragma once
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <fstream>
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
#include "VideoPlayer.h"

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
    ReplayFactory,
    Loading  // Async beatmap loading state
};

// Beatmap loading state
enum class LoadingState {
    Idle,
    Parsing,        // Parsing chart file
    ExtractingAudio,// Extracting audio (MUSYNX ACB, etc.)
    LoadingAudio,   // Loading music file
    LoadingKeysounds,// Loading keysounds
    LoadingAssets,  // Loading storyboard, textures
    Completed,
    Failed,
    Cancelled
};

// Beatmap source type
enum class BeatmapSource {
    Osu,
    DJMaxRespect,  // DJMAX RESPECT (.bytes files)
    DJMaxOnline,   // DJMAX Online (.pt files)
    O2Jam,
    BMS,
    Malody,
    MuSynx,
    IIDX,          // beatmania IIDX (.1 files)
    StepMania,     // StepMania (.sm/.ssc files)
    SDVX,          // Sound Voltex (.vox files)
    EZ2AC          // EZ2AC (.ez files)
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
    // Per-difficulty background and audio (may differ from song-level defaults)
    std::string backgroundPath;  // Background image path for this difficulty
    std::string audioPath;       // Audio file path for this difficulty
    int previewTime = 0;         // Preview start time in ms (-1 = 40% position)
    // Metadata for header display
    int totalLength = 0;         // Song length in ms (last note time)
    double bpmMin = 0;           // Minimum BPM
    double bpmMax = 0;           // Maximum BPM
    double bpmMost = 0;          // Most dominant BPM (longest duration)
    int totalObjects = 0;        // Total note count
    int rcCount = 0;             // Regular (tap) note count
    int lnCount = 0;             // Long note count
    float od = 0;                // Overall Difficulty
    float hp = 0;                // HP Drain
};

// Song entry for song select screen
struct SongEntry {
    std::string folderPath;      // Full path to song folder
    std::string folderName;      // Folder name (for display)
    std::string title;           // Song title (from beatmap)
    std::string titleUnicode;    // Song title (Unicode version)
    std::string artist;          // Artist name (from beatmap)
    std::string artistUnicode;   // Artist name (Unicode version)
    std::string backgroundPath;  // Path to background image
    std::string audioPath;       // Path to audio file
    std::string sourceText;      // Source metadata (e.g. "Touhou", "Vocaloid")
    std::string tags;            // Space-separated tags
    int previewTime = 0;         // Preview start time in ms
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
    bool loadBeatmap(const std::string& path, bool skipParsing = false);
    void resetGame();
    void cleanupTempDir();  // Clean up BGA textures when switching songs
    void cleanupTempFiles();  // Clean up temp files (startup/exit only)
    void updateBga(int64_t currentTime);  // Update BGA state
    void renderBga();  // Render BGA layers
    void saveConfig();
    void loadConfig();
    std::string openFileDialog();
    std::string openReplayDialog();
    std::string saveReplayDialog();
    std::string saveImageDialog();
    std::string openSkinFolderDialog();
    void exportBeatmap();  // Export selected song to osu! format
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

    // osu! background video
    VideoPlayer osuVideoPlayer;
    int osuVideoOffset = 0;  // Video start offset in ms

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
    bool laneKeyDown[18];  // track key state for each lane (up to 18k)
    int laneNextNoteIndex[18];  // index of next note in each lane (for empty tap keysound)

    Settings settings;
    SettingsCategory settingsCategory;
    int keyBindingIndex;
    int editingValue;
    bool editingVolume;
    bool dropdownExpanded;          // Output Device dropdown
    bool audioModeDropdownExpanded;  // Audio Mode dropdown
    bool asioDeviceDropdownExpanded; // ASIO Device dropdown
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
    std::string songSelectSearch;  // Search filter text
    std::vector<int> filteredSongIndices;  // Indices into songList matching current search
    std::vector<std::vector<int>> filteredDiffIndices;  // Per-song matching difficulty indices (parallel to filteredSongIndices)
    int selectedSongIndex;
    int selectedDifficultyIndex;  // Selected difficulty within the song
    float songSelectScroll;  // Scroll offset for song list
    bool songSelectNeedAutoScroll;  // Flag to trigger auto-scroll on selection change
    bool songSelectDragging;  // Mouse drag scrolling
    int songSelectDragStartY;  // Mouse Y when drag started
    float songSelectDragStartScroll;  // Scroll value when drag started
    SDL_Texture* currentBgTexture;  // Background texture for selected song
    // Async background loading
    std::thread bgLoadThread;
    std::atomic<bool> bgLoadPending{false};
    std::mutex bgLoadMutex;
    unsigned char* bgLoadData = nullptr;
    int bgLoadWidth = 0;
    int bgLoadHeight = 0;
    bool songSelectTransition;  // True when transitioning out
    int64_t songSelectTransitionStart;  // Transition start time
    void scanSongsFolder();
    void updateSongFilter();  // Rebuild filteredSongIndices from songSelectSearch

    // Async Song Scanning
    std::thread scanThread;
    std::atomic<bool> scanRunning{false};
    std::atomic<int> scanProgress{0};
    std::atomic<int> scanTotal{0};
    std::string scanStatusText;
    std::mutex scanMutex;
    GameState stateAfterScan = GameState::Menu;
    void startScanAsync(bool clearIndex, GameState afterState);
    void finalizeScan();
    void loadSongBackground(int songIndex, int diffIndex = -1);
    void updateBackgroundLoad();  // Check async background load completion
    void playPreviewMusic(int songIndex, int diffIndex = -1);
    void stopPreviewMusic();
    void updatePreviewFade();  // Update fade in/out

    // Preview fade
    bool previewFading;
    bool previewFadeIn;  // true = fade in, false = fade out
    int64_t previewFadeStart;
    int previewFadeDuration;
    int previewTargetIndex;  // Song to play after fade out
    int previewTargetDiffIndex;  // Difficulty to play after fade out
    std::string currentPreviewAudioPath;  // Currently playing audio path (to avoid reloading same audio)

    // Replay Factory
    std::string factoryReplayPath;  // Loaded replay file path
    ReplayInfo factoryReplayInfo;   // Loaded replay data
    ReplayInfo factoryReplayInfoMirrored;  // Mirrored replay data
    bool factoryMirrorInput = false; // Mirror input checkbox state

    // Fix Hash difficulty selection
    bool factoryFixHashPending = false;  // Waiting for difficulty selection
    std::string factoryFixHashPath;      // File path pending hash fix
    int factoryFixHashType = 0;          // 0=none, 1=O2Jam, 2=IIDX

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
    std::ofstream lockFile;  // For single instance detection
    bool editingBlockHeight = false;
    std::string blockHeightInput = "40";
    bool editingVideoWidth = false;
    std::string videoWidthInput = "540";
    bool editingVideoHeight = false;
    std::string videoHeightInput = "960";
    bool editingVideoFPS = false;
    std::string videoFPSInput = "60";
    bool videoShowHolding = false;

    // Async Beatmap Loading
    std::thread loadingThread;
    std::atomic<LoadingState> loadingState{LoadingState::Idle};
    std::atomic<float> loadingProgress{0.0f};
    std::atomic<bool> loadingCancelled{false};
    std::string loadingStatusText;
    std::mutex loadingMutex;
    std::string pendingBeatmapPath;
    GameState stateBeforeLoading = GameState::SongSelect;
    bool pendingReplayMode = false;  // True if loading for replay playback

    void startAsyncLoad(const std::string& path, bool isReplayMode = false);
    void loadBeatmapAsync(const std::string& path);
    void cancelLoading();

    // Async Export
    bool pendingExport = false;  // True when Loading state is for export (not beatmap load)
    void exportBeatmapAsync();

    // Debug logging
    std::vector<DebugLogEntry> debugLog;
    void addDebugLog(int64_t time, const std::string& eventType, int lane, const std::string& details);
    void exportDebugLog();
};
