#include "Game.h"
#include "ReplayWriter.h"
#include "BeatmapConverter.h"
#include "DJMaxParser.h"
#include "PTParser.h"
#include "PakExtractor.h"
#include "OjnParser.h"
#include "OjmParser.h"
#include "BMSParser.h"
#include "MalodyParser.h"
#include "MuSynxParser.h"
#include "AcbParser.h"
#include "2dxParser.h"
#include "IIDXSongDB.h"
#include "StarRating.h"
#include "SongIndex.h"
#include "OsuMods.h"
#include "FileDialog.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cmath>
#include <numeric>
#include <algorithm>

// ICU for locale-aware string comparison
#include <unicode/ucol.h>
#include <unicode/ustring.h>

namespace fs = std::filesystem;

// Helper function to get timing point sample set at a given time
static SampleSet getTimingPointSampleSet(const std::vector<TimingPoint>& timingPoints, int64_t time) {
    SampleSet result = SampleSet::Normal;  // Default
    for (const auto& tp : timingPoints) {
        if (tp.time <= time) {
            result = tp.sampleSet;
        } else {
            break;
        }
    }
    return result;
}

// Helper function to get BPM at a given time (for O2Jam judgement)
static double getBPMAtTime(const std::vector<TimingPoint>& timingPoints, int64_t time, double defaultBPM) {
    double bpm = defaultBPM;
    for (const auto& tp : timingPoints) {
        if (tp.time <= time && tp.uninherited && tp.beatLength > 0) {
            bpm = 60000.0 / tp.beatLength;
        } else if (tp.time > time) {
            break;
        }
    }
    return bpm;
}

// Helper function to calculate overlap percentage between note and virtual judge line
// noteY: top of actual note, judgeLineY: center of virtual judge line (real judge line position)
// Both virtual note and virtual judge line are scaled by Hi-Speed, centered on their actual positions
static double calcOverlapPercent(int noteY, int judgeLineY, int noteHeight, double speedMultiplier = 1.0) {
    // Virtual note height scales with speed, centered on actual note
    int virtualNoteHeight = static_cast<int>(noteHeight * speedMultiplier);
    int actualNoteCenter = noteY + noteHeight / 2;
    int noteTop = actualNoteCenter - virtualNoteHeight / 2;
    int noteBottom = actualNoteCenter + virtualNoteHeight / 2;

    // Virtual judge line height scales with speed, centered on real judge line
    int judgeHeight = static_cast<int>(noteHeight * speedMultiplier);
    int judgeTop = judgeLineY - judgeHeight / 2;
    int judgeBottom = judgeLineY + judgeHeight / 2;

    // Calculate overlap
    int overlapTop = std::max(noteTop, judgeTop);
    int overlapBottom = std::min(noteBottom, judgeBottom);
    int overlap = std::max(0, overlapBottom - overlapTop);

    // Return overlap as percentage of virtual note height
    return (double)overlap / virtualNoteHeight;
}

// Helper function to build HitSoundInfo from Note
static HitSoundInfo buildHitSoundInfo(const Note& note, const std::vector<TimingPoint>& timingPoints, int64_t time, bool isTail = false) {
    HitSoundInfo info;
    if (isTail) {
        info.sampleSet = note.tailSampleSet;
        info.additionSet = note.tailAdditions;
        info.customIndex = note.tailCustomIndex;
    } else {
        info.sampleSet = note.sampleSet;
        info.additionSet = note.additions;
        info.customIndex = note.customIndex;
    }
    // If sampleSet is None, use timing point's sampleSet
    if (info.sampleSet == SampleSet::None) {
        info.sampleSet = getTimingPointSampleSet(timingPoints, time);
    }
    // Build hitSoundFlags
    info.hitSoundFlags = static_cast<int>(HitSoundType::Normal);
    if (note.hasWhistle) info.hitSoundFlags |= static_cast<int>(HitSoundType::Whistle);
    if (note.hasClap) info.hitSoundFlags |= static_cast<int>(HitSoundType::Clap);
    if (note.hasFinish) info.hitSoundFlags |= static_cast<int>(HitSoundType::Finish);
    return info;
}

// Helper function to build HitSoundInfo for empty tap (no note hit)
// Use the next note's hitsound info for trigger matching
static HitSoundInfo buildEmptyTapHitSoundInfo(const std::vector<TimingPoint>& timingPoints, int64_t time, const Note& nextNote) {
    HitSoundInfo info;
    // Use next note's sampleSet, or fall back to timing point
    info.sampleSet = (nextNote.sampleSet != SampleSet::None)
                     ? nextNote.sampleSet
                     : getTimingPointSampleSet(timingPoints, time);
    info.additionSet = nextNote.additions;
    info.customIndex = nextNote.customIndex;
    info.isEmptyTap = false;

    // Build hitSoundFlags from next note
    info.hitSoundFlags = static_cast<int>(HitSoundType::Normal);
    if (nextNote.hasWhistle) info.hitSoundFlags |= static_cast<int>(HitSoundType::Whistle);
    if (nextNote.hasClap) info.hitSoundFlags |= static_cast<int>(HitSoundType::Clap);
    if (nextNote.hasFinish) info.hitSoundFlags |= static_cast<int>(HitSoundType::Finish);

    return info;
}

// Helper function to update next note index for a lane after a note is hit
static void updateLaneNextNoteIndex(int* laneNextNoteIndex, const std::vector<Note>& notes, int lane, int currentIndex) {
    // Find the next Waiting note in this lane after currentIndex
    for (size_t i = currentIndex + 1; i < notes.size(); i++) {
        if (notes[i].lane == lane && !notes[i].isFakeNote) {
            laneNextNoteIndex[lane] = static_cast<int>(i);
            return;
        }
    }
    // No more notes in this lane - keep the last index (for keysound)
}

// Helper function to format difficulty name (separate function to avoid optimizer issues)
static std::string formatDifficultyName(const SongEntry& song, int d, int starRatingVersion) {
    std::string diffName;
    if (d >= 0 && d < (int)song.difficulties.size()) {
        const auto& diff = song.difficulties[d];
        diffName = diff.version;
        // Don't show creator for BMS (already included in version)
        if (!diff.creator.empty() && song.source != BeatmapSource::BMS) {
            diffName += " (" + diff.creator + ")";
        }
        diffName += " [" + std::to_string(diff.keyCount) + "K]";
        char starStr[32];
        snprintf(starStr, sizeof(starStr), " %.2f", diff.starRatings[starRatingVersion]);
        diffName += starStr;
        diffName += "\xe2\x98\x85";
    } else if (d >= 0 && d < (int)song.beatmapFiles.size()) {
        diffName = song.beatmapFiles[d];
        size_t lastSlash = diffName.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            diffName = diffName.substr(lastSlash + 1);
        }
    }
    return diffName;
}

// Helper function to convert settings int to StarRatingVersion enum
static StarRatingVersion getStarRatingVersion(int settingValue) {
    switch (settingValue) {
        case 1: return StarRatingVersion::OsuStable_b20220101;
        case 0:
        default: return StarRatingVersion::OsuStable_b20260101;
    }
}

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#endif

Game::Game() : state(GameState::Menu), running(false), musicStarted(false), hasBackgroundMusic(true), autoPlay(false),
               baseBPM(120.0), clockRate(1.0), currentStarRating(0.0), scoreMultiplier(1.0),
               startTime(0), combo(0), maxCombo(0), score(0), scoreAccumulator(0.0), bonus(100.0), totalNotes(0),
               lastJudgementTime(0), lastComboChangeTime(0), comboBreak(false), comboBreakTime(0), lastComboValue(0),
               anyHoldActive(false), holdColorChangeTime(0),
               fps(0), frameCount(0), lastFpsTime(0), totalTime(0),
               lastFrameTime(0), targetFrameDelay(1),
               perfInput(0), perfUpdate(0), perfDraw(0), perfAudio(0),
               mouseX(0), mouseY(0), mouseClicked(false), mouseDown(false),
               settingsCategory(SettingsCategory::Sound), keyBindingIndex(0),
               editingValue(0), editingVolume(false),
               dropdownExpanded(false), judgeModeDropdownExpanded(false),
               resolutionDropdownExpanded(false), refreshRateDropdownExpanded(false),
               keyCountDropdownExpanded(false),
               starRatingDropdownExpanded(false),
               settingsScroll(0), settingsDragging(false), settingsDragStartY(0), settingsDragStartScroll(0),
               judgeDetailPopup(-1), pauseMenuSelection(0), pauseTime(0),
               pauseFadingOut(false), pauseFadeOutStart(0), pauseAudioOffset(0),
               replayMode(false), currentReplayFrame(0),
               lastRecordedKeyState(0),
               allNotesFinishedTime(0), showEndPrompt(false), editingUsername(false),
               editingScrollSpeed(false),
               selectedSongIndex(0), selectedDifficultyIndex(0), songSelectScroll(0.0f), songSelectNeedAutoScroll(false),
               songSelectDragging(false), songSelectDragStartY(0), songSelectDragStartScroll(0.0f), currentBgTexture(nullptr),
               songSelectTransition(false), songSelectTransitionStart(0),
               previewFading(false), previewFadeIn(true), previewFadeStart(0),
               previewFadeDuration(200), previewTargetIndex(-1), previewTargetDiffIndex(-1),
               hasBga(false), currentBgaEntry(0) {
    for (int i = 0; i < 6; i++) {
        judgementCounts[i] = 0;
    }
    for (int i = 0; i < 10; i++) {
        laneKeyDown[i] = false;
    }
    // Set default colors for 4k
    settings.setDefaultColors(4);
}

Game::~Game() {
    // Wait for background load thread to finish
    if (bgLoadThread.joinable()) {
        bgLoadThread.join();
    }
    if (bgLoadData) {
        stbi_image_free(bgLoadData);
        bgLoadData = nullptr;
    }
    cleanupTempFiles();  // Clean up temp files on exit
    TTF_Quit();
    SDL_Quit();
}

bool Game::init() {
    // Prevent multiple instances using lock file
    std::string lockPath = "Data/.lock";
    fs::create_directories("Data");
    lockFile.open(lockPath, std::ios::out);
    if (!lockFile.is_open()) {
        return false;  // Another instance may be running
    }
#ifdef _WIN32
    // On Windows, opening the file is enough - it will fail if another process has it open
    // But we need to keep the file open for the duration of the program
#else
    // On Unix, use flock (would need additional implementation)
#endif

    // Clean up temp files from previous session
    cleanupTempFiles();

    // Load config first
    loadConfig();

#ifdef _WIN32
    // Show console if debug is enabled
    if (settings.debugEnabled) {
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
        return false;
    }
    if (!TTF_Init()) {
        std::cerr << "TTF init failed" << std::endl;
        return false;
    }
    if (!renderer.init()) {
        std::cerr << "Renderer init failed" << std::endl;
        return false;
    }

    // Apply saved resolution
    if (settings.resolution >= 0 && settings.resolution <= 2) {
        int widths[] = {1280, 1920, 2560};
        int heights[] = {720, 1080, 1440};
        renderer.setResolution(widths[settings.resolution], heights[settings.resolution]);
    }
    if (!audio.init()) {
        std::cerr << "Audio init failed" << std::endl;
        return false;
    }

    // Initialize key sound manager
    keySoundManager.setAudioManager(&audio);

    // Initialize skin manager
    renderer.setSkinManager(&skinManager);
    if (!settings.skinPath.empty()) {
        skinManager.loadSkin(settings.skinPath, renderer.getRenderer());
    }

    // Load song index at startup
    scanSongsFolder();

    return true;
}

std::string Game::openFileDialog() {
    return FileDialog::openFile(
        "Select Beatmap",
        nullptr,
        {"*.osu", "*.bytes", "*.ojn", "*.pt", "*.bms", "*.bme", "*.bml", "*.pms", "*.1", "*.mc", "*.txt"},
        "Beatmap Files"
    );
}

std::string Game::openReplayDialog() {
    return FileDialog::openFile(
        "Select Replay",
        nullptr,
        {"*.osr"},
        "osu! Replay"
    );
}

std::string Game::saveReplayDialog() {
    return FileDialog::saveFile(
        "Save Replay",
        "replay.osr",
        {"*.osr"},
        "osu! Replay"
    );
}

std::string Game::saveImageDialog() {
    return FileDialog::saveFile(
        "Save Image",
        "analysis.png",
        {"*.png"},
        "PNG Image"
    );
}

std::string Game::openSkinFolderDialog() {
    return FileDialog::selectFolder("Select Skin Folder");
}

void Game::saveConfig() {
    std::ofstream file("config.ini");
    if (!file.is_open()) return;

    file << "[Sound]\n";
    file << "volume=" << settings.volume << "\n";
    file << "audioDevice=" << settings.audioDevice << "\n";
    file << "audioOffset=" << settings.audioOffset << "\n";

    file << "\n[Graphics]\n";
    file << "resolution=" << settings.resolution << "\n";
    file << "refreshRate=" << settings.refreshRate << "\n";
    file << "vsync=" << (settings.vsync ? 1 : 0) << "\n";
    file << "borderlessFullscreen=" << (settings.borderlessFullscreen ? 1 : 0) << "\n";
    file << "laneLight=" << (settings.laneLight ? 1 : 0) << "\n";

    file << "\n[Input]\n";
    file << "selectedKeyCount=" << settings.selectedKeyCount << "\n";
    file << "n1Style=" << (settings.n1Style ? 1 : 0) << "\n";
    file << "mirror=" << (settings.mirror ? 1 : 0) << "\n";
    // Save keys for each key count
    for (int k = 0; k < 10; k++) {
        file << "keys" << (k + 1) << "=";
        for (int i = 0; i <= k; i++) {
            if (i > 0) file << ",";
            file << (int)settings.keys[k][i];
        }
        file << "\n";
    }

    file << "\n[Judgement]\n";
    file << "judgeMode=" << (int)settings.judgeMode << "\n";
    file << "customOD=" << settings.customOD << "\n";
    file << "noteLock=" << (settings.noteLock ? 1 : 0) << "\n";
    file << "legacyHoldJudgement=" << (settings.legacyHoldJudgement ? 1 : 0) << "\n";
    file << "hitErrorBarScale=" << settings.hitErrorBarScale << "\n";
    // Save custom judgement windows
    for (int i = 0; i < 6; i++) {
        file << "judgeWindow" << i << "=" << settings.judgements[i].window << "\n";
        file << "judgeBreaksCombo" << i << "=" << (settings.judgements[i].breaksCombo ? 1 : 0) << "\n";
        file << "judgeEnabled" << i << "=" << (settings.judgements[i].enabled ? 1 : 0) << "\n";
    }

    file << "\n[Scroll]\n";
    file << "scrollSpeed=" << settings.scrollSpeed << "\n";
    file << "bpmScaleMode=" << (settings.bpmScaleMode ? 1 : 0) << "\n";
    file << "unlimitedSpeed=" << (settings.unlimitedSpeed ? 1 : 0) << "\n";

    file << "\n[Skin]\n";
    file << "skinPath=" << settings.skinPath << "\n";
    file << "ignoreBeatmapSkin=" << (settings.ignoreBeatmapSkin ? 1 : 0) << "\n";
    file << "ignoreBeatmapHitsounds=" << (settings.ignoreBeatmapHitsounds ? 1 : 0) << "\n";
    file << "disableStoryboard=" << (settings.disableStoryboard ? 1 : 0) << "\n";
    file << "backgroundDim=" << settings.backgroundDim << "\n";

    file << "\n[Misc]\n";
    file << "username=" << settings.username << "\n";
    file << "forceOverrideUsername=" << (settings.forceOverrideUsername ? 1 : 0) << "\n";
    file << "debugEnabled=" << (settings.debugEnabled ? 1 : 0) << "\n";
    file << "ignoreSV=" << (settings.ignoreSV ? 1 : 0) << "\n";

    file << "\n[StarRating]\n";
    file << "starRatingVersion=" << settings.starRatingVersion << "\n";

    file.close();;
}

void Game::loadConfig() {
    std::ifstream file("config.ini");
    if (!file.is_open()) return;

    std::string line, section;
    while (std::getline(file, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        // Skip comments
        if (line[0] == ';' || line[0] == '#') continue;

        // Section header
        if (line[0] == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        // Key=value
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        try {
            if (section == "Sound") {
                if (key == "volume") settings.volume = std::stoi(value);
                else if (key == "audioDevice") settings.audioDevice = std::stoi(value);
                else if (key == "audioOffset") settings.audioOffset = std::stoi(value);
            }
            else if (section == "Graphics") {
                if (key == "resolution") settings.resolution = std::stoi(value);
                else if (key == "refreshRate") settings.refreshRate = std::stoi(value);
                else if (key == "vsync") settings.vsync = (value == "1");
                else if (key == "borderlessFullscreen") settings.borderlessFullscreen = (value == "1");
                else if (key == "laneLight") settings.laneLight = (value == "1");
            }
            else if (section == "Input") {
                if (key == "selectedKeyCount") settings.selectedKeyCount = std::stoi(value);
                else if (key == "n1Style") settings.n1Style = (value == "1");
                else if (key == "mirror") settings.mirror = (value == "1");
                else if (key.find("keys") == 0 && key.size() > 4) {
                    int k = std::stoi(key.substr(4)) - 1;
                    if (k >= 0 && k < 10) {
                        std::stringstream ss(value);
                        std::string token;
                        int i = 0;
                        while (std::getline(ss, token, ',') && i <= k) {
                            settings.keys[k][i++] = (SDL_Keycode)std::stoi(token);
                        }
                    }
                }
            }
            else if (section == "Judgement") {
                if (key == "judgeMode") settings.judgeMode = (JudgementMode)std::stoi(value);
                else if (key == "customOD") settings.customOD = std::stof(value);
                else if (key == "noteLock") settings.noteLock = (value == "1");
                else if (key == "legacyHoldJudgement") settings.legacyHoldJudgement = (value == "1");
                else if (key == "hitErrorBarScale") settings.hitErrorBarScale = std::stof(value);
                // Load custom judgement windows
                else if (key.find("judgeWindow") == 0) {
                    int idx = std::stoi(key.substr(11));
                    if (idx >= 0 && idx < 6) settings.judgements[idx].window = std::stoll(value);
                }
                else if (key.find("judgeBreaksCombo") == 0) {
                    int idx = std::stoi(key.substr(16));
                    if (idx >= 0 && idx < 6) settings.judgements[idx].breaksCombo = (value == "1");
                }
                else if (key.find("judgeEnabled") == 0) {
                    int idx = std::stoi(key.substr(12));
                    if (idx >= 0 && idx < 6) settings.judgements[idx].enabled = (value == "1");
                }
            }
            else if (section == "Scroll") {
                if (key == "scrollSpeed") settings.scrollSpeed = std::stoi(value);
                else if (key == "bpmScaleMode") settings.bpmScaleMode = (value == "1");
                else if (key == "unlimitedSpeed") settings.unlimitedSpeed = (value == "1");
            }
            else if (section == "Skin") {
                if (key == "skinPath") settings.skinPath = value;
                else if (key == "ignoreBeatmapSkin") settings.ignoreBeatmapSkin = (value == "1");
                else if (key == "ignoreBeatmapHitsounds") settings.ignoreBeatmapHitsounds = (value == "1");
                else if (key == "disableStoryboard") settings.disableStoryboard = (value == "1");
                else if (key == "backgroundDim") settings.backgroundDim = std::stoi(value);
            }
            else if (section == "Misc") {
                if (key == "username") settings.username = value;
                else if (key == "forceOverrideUsername") settings.forceOverrideUsername = (value == "1");
                else if (key == "debugEnabled") settings.debugEnabled = (value == "1");
                else if (key == "ignoreSV") settings.ignoreSV = (value == "1");
            }
            else if (section == "StarRating") {
                if (key == "starRatingVersion") settings.starRatingVersion = std::stoi(value);
            }
        } catch (...) {
            // Ignore parsing errors
        }
    }
    file.close();
}

void Game::resetGame() {
    // Note: beatmap data (notes, timingPoints, etc.) is cleared in loadBeatmap
    // when skipParsing=false, not here, to preserve async-loaded data
    for (int i = 0; i < 6; i++) judgementCounts[i] = 0;
    // Reset lane key states to prevent ghost key presses at game start
    for (int i = 0; i < 10; i++) {
        laneKeyDown[i] = false;
    }
    combo = 0;
    maxCombo = 0;
    score = 0;
    scoreAccumulator = 0.0;
    bonus = 100.0;
    totalTime = 0;
    hitErrors.clear();
    lastJudgementText = "";
    lastJudgementIndex = -1;
    musicStarted = false;
    allNotesFinishedTime = 0;
    showEndPrompt = false;
    recordedFrames.clear();
    lastRecordedKeyState = 0;
    renderer.resetHitErrorIndicator();
    renderer.resetKeyReleaseTime();  // Reset key image states
    debugLog.clear();
    hpManager.reset();
    // Note: keySoundManager.clear() and audio.clearSamples() moved to loadBeatmap
    // when skipParsing=false, to preserve async-loaded keysounds
    deathTime = 0;
    deathSlowdown = 1.0f;
    deathMenuSelection = 1;
    pauseAudioOffset = 0;
    pauseFadingOut = false;
    skipTargetTime = 0;
    canSkip = false;
    currentReplayFrame = 0;  // Reset replay frame index for retry
    currentStoryboardSample = 0;  // Reset storyboard sample index
    currentBgaEntry = 0;  // Reset BGA timeline index
    bgaData.layers.clear();  // Clear BGA layer states
    bgaData.timeline.clear();  // Clear BGA timeline
    hasBga = false;  // Reset BGA flag
    isBmsBga = false;  // Reset BMS BGA flag
    bmsBgaManager.clear();  // Clear BMS BGA resources
    // Clear BGA textures
    for (auto& [name, tex] : bgaTextures) {
        if (tex) SDL_DestroyTexture(tex);
    }
    bgaTextures.clear();
    lastComboChangeTime = 0;
    comboBreak = false;
    comboBreakTime = 0;
    lastComboValue = 0;
    anyHoldActive = false;
    holdColorChangeTime = 0;
    ppCalculator.reset();
    // Initialize next note index for each lane
    for (int i = 0; i < 10; i++) {
        laneNextNoteIndex[i] = -1;  // Will be set after loading beatmap
    }
}

bool Game::loadBeatmap(const std::string& path, bool skipParsing) {
    // Always reset game state (score, combo, etc.) regardless of skipParsing
    resetGame();
    beatmapPath = path;
    if (!skipParsing) {
        beatmap = BeatmapInfo();  // Clear previous beatmap data
        keySoundManager.clear();  // Clear keysounds only when re-parsing
        audio.clearSamples();     // Clear audio samples only when re-parsing
    }

    // Stop any playing audio first (preview music)
    audio.stop();

    // Calculate clockRate early so audio loading uses correct playback rate
    clockRate = 1.0;
    scoreMultiplier = 1.0;
    // Apply DT/NC/HT only if not in replay mode (replay mode uses mods from replay file)
    // Priority: DT > HT (same as osu!)
    if (!replayMode) {
        if (settings.doubleTimeEnabled || settings.nightcoreEnabled) {
            clockRate = 1.5;
        } else if (settings.halfTimeEnabled) {
            clockRate = 0.75;
            scoreMultiplier = 0.5;
        }
        audio.setPlaybackRate(static_cast<float>(clockRate));
        audio.setChangePitch(settings.nightcoreEnabled);
    }

    // Check for O2Jam difficulty suffix (path.ojn:difficulty:level)
    OjnDifficulty ojnDifficulty = OjnDifficulty::Hard;
    std::string actualPath = path;
    size_t colonPos = path.rfind(':');
    if (colonPos != std::string::npos && colonPos > 2) {
        // Find second-to-last colon for O2Jam format
        size_t colonPos2 = path.rfind(':', colonPos - 1);
        if (colonPos2 != std::string::npos && colonPos2 > 2) {
            // Format: path.ojn:difficulty:level
            std::string diffStr = path.substr(colonPos2 + 1, colonPos - colonPos2 - 1);
            if (diffStr == "0") ojnDifficulty = OjnDifficulty::Easy;
            else if (diffStr == "1") ojnDifficulty = OjnDifficulty::Normal;
            else if (diffStr == "2") ojnDifficulty = OjnDifficulty::Hard;
            actualPath = path.substr(0, colonPos2);
        }
    }

    // Check file type
    bool isDJMax = DJMaxParser::isDJMaxChart(actualPath);
    bool isPT = PTParser::isPTFile(actualPath);
    bool isOjn = OjnParser::isOjnFile(actualPath);
    bool isBMS = BMSParser::isBMSFile(actualPath);
    bool isIIDX = actualPath.size() > 2 && actualPath.substr(actualPath.size() - 2) == ".1";
    bool isMalody = actualPath.size() > 3 && actualPath.substr(actualPath.size() - 3) == ".mc";
    bool isMuSynx = actualPath.size() > 4 && actualPath.substr(actualPath.size() - 4) == ".txt" &&
                   (actualPath.find("4T_") != std::string::npos || actualPath.find("6T_") != std::string::npos);
    std::string ptAudioDir;  // For PT files with PAK extraction

    if (!skipParsing) {
    if (isDJMax) {
        if (!DJMaxParser::parse(actualPath, beatmap)) {
            std::cerr << "Failed to parse DJMAX chart" << std::endl;
            return false;
        }
        std::cout << "Loaded DJMAX chart: " << beatmap.keyCount << "K" << std::endl;
    } else if (isPT) {
        if (!PTParser::parse(actualPath, beatmap)) {
            std::cerr << "Failed to parse PT chart" << std::endl;
            return false;
        }
        std::cout << "Loaded PT chart: " << beatmap.keyCount << "K" << std::endl;

        // Load PAK file for key sounds
        fs::path ptPath(actualPath);
        std::string stemStr = ptPath.stem().string();
        size_t underscorePos = stemStr.find('_');
        std::string baseName = (underscorePos != std::string::npos) ? stemStr.substr(0, underscorePos) : stemStr;
        fs::path pakPath = ptPath.parent_path() / (baseName + ".pak");

        // Try to find any .pak file in the same directory
        if (!fs::exists(pakPath)) {
            for (const auto& entry : fs::directory_iterator(ptPath.parent_path())) {
                if (entry.path().extension() == ".pak") {
                    pakPath = entry.path();
                    break;
                }
            }
        }

        if (fs::exists(pakPath)) {
            static PakExtractor pakExtractor;
            static bool keysLoaded = false;

            if (!keysLoaded) {
                keysLoaded = pakExtractor.loadKeys("D:\\work\\DJMax_Online\\Xip-Pak-Extractor-main\\keyFiles");
            }

            if (keysLoaded && pakExtractor.open(pakPath.string())) {
                fs::path tempDir = fs::current_path() / "Data" / "Tmp" / pakPath.stem().string();
                fs::create_directories(tempDir);
                std::cout << "Temp dir: " << tempDir.string() << std::endl;
                std::cout << "File count: " << pakExtractor.getFileList().size() << std::endl;

                int extractedAudio = 0;
                int extractedBga = 0;
                for (const auto& fileEntry : pakExtractor.getFileList()) {
                    std::string ext = fs::path(fileEntry.filename).extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                    // Check if this is an audio or BGA file
                    bool isAudio = (ext == ".ogg" || ext == ".wav" || ext == ".mp3");
                    bool isBga = (ext == ".vcq" || ext == ".vce" || ext == ".png" || ext == ".jpg" || ext == ".jpeg");

                    if (isAudio || isBga) {
                        std::vector<uint8_t> data;
                        if (pakExtractor.extractFile(fileEntry.filename, data)) {
                            fs::path outPath;
                            if (isAudio) {
                                // Audio files go to root of tempDir (flat)
                                outPath = tempDir / fs::path(fileEntry.filename).filename();
                            } else {
                                // BGA files preserve directory structure
                                outPath = tempDir / fs::path(fileEntry.filename);
                                fs::create_directories(outPath.parent_path());
                            }

                            std::ofstream out(outPath, std::ios::binary);
                            if (out) {
                                out.write(reinterpret_cast<char*>(data.data()), data.size());
                                out.close();
                                if (isAudio) extractedAudio++;
                                else extractedBga++;
                            }
                        }
                    }
                }
                std::cout << "Extracted " << extractedAudio << " audio, " << extractedBga << " BGA files from PAK" << std::endl;
                ptAudioDir = tempDir.string();

                // Load BGA if available (temporarily disabled)
                hasBga = false;
                // TODO: Re-enable BGA when animation is fixed
                /*
                // BGA files are in song/{songName}/BGA/
                fs::path bgaDir = tempDir / "song" / pakPath.stem().string() / "BGA";
                std::cout << "Looking for BGA dir: " << bgaDir.string() << std::endl;

                if (fs::exists(bgaDir)) {
                    std::string songName = pakPath.stem().string();
                    std::string vcqPath = BgaParser::getVcqPath(bgaDir.string(), songName);
                    if (!vcqPath.empty()) {
                        if (BgaParser::parseVcq(vcqPath, bgaData.timeline)) {
                            bgaData.bgaDir = bgaDir.string();
                            bgaData.layers.clear();
                            currentBgaEntry = 0;
                            hasBga = true;
                            std::cout << "Loaded BGA: " << bgaData.timeline.size() << " timeline entries" << std::endl;
                        }
                    }
                }
                */

                pakExtractor.close();
            }
        }
    } else if (isOjn) {
        if (!OjnParser::parse(actualPath, beatmap, ojnDifficulty)) {
            std::cerr << "Failed to parse O2Jam chart" << std::endl;
            return false;
        }
        std::cout << "Loaded O2Jam chart: " << beatmap.keyCount << "K" << std::endl;

        // Load OJM file for key sounds
        std::string ojmPath = OjmParser::getOjmPath(actualPath);
        if (!ojmPath.empty()) {
            OjmInfo ojmInfo;
            if (OjmParser::parse(ojmPath, ojmInfo)) {
                std::cout << "Loaded OJM: " << ojmInfo.samples.size() << " samples" << std::endl;

                // Save samples to temp files and load into AudioManager
                fs::path tempDir = fs::current_path() / "Data" / "Tmp" / "ojm";
                fs::create_directories(tempDir);

                std::unordered_map<int, int> sampleIdToHandle;
                for (auto& [id, sample] : ojmInfo.samples) {
                    std::string ext = sample.isOgg ? ".ogg" : ".wav";
                    fs::path tempFile = tempDir / (std::to_string(id) + ext);

                    std::ofstream out(tempFile, std::ios::binary);
                    if (out) {
                        out.write(reinterpret_cast<const char*>(sample.data.data()), sample.data.size());
                        out.close();

                        int handle = audio.loadSample(tempFile.string());
                        if (handle >= 0) {
                            sampleIdToHandle[id] = handle;
                        }
                    }
                }

                // Debug: print OJM sample IDs
                std::cout << "OJM sample IDs: ";
                for (auto& [id, handle] : sampleIdToHandle) {
                    std::cout << id << " ";
                }
                std::cout << std::endl;

                // Debug: print storyboard sample IDs before mapping
                std::cout << "BGM sample IDs needed: ";
                for (auto& sample : beatmap.storyboardSamples) {
                    if (sample.sampleHandle > 0) {
                        std::cout << sample.sampleHandle << " ";
                    }
                }
                std::cout << std::endl;

                // Update notes with sample handles
                for (auto& note : beatmap.notes) {
                    if (note.customIndex > 0) {
                        auto it = sampleIdToHandle.find(note.customIndex);
                        if (it != sampleIdToHandle.end()) {
                            note.sampleHandle = it->second;
                        }
                    }
                }

                // Update storyboard samples (BGM) with sample handles
                for (auto& sample : beatmap.storyboardSamples) {
                    // sampleHandle contains the OJM sample ID (keysound: 2+, BGM: 1002+)
                    // fallbackHandle contains the fallback ID
                    if (sample.sampleHandle > 0) {
                        int primaryId = sample.sampleHandle;
                        int fallbackId = sample.fallbackHandle;

                        auto it = sampleIdToHandle.find(primaryId);
                        if (it != sampleIdToHandle.end()) {
                            sample.sampleHandle = it->second;
                        } else if (fallbackId > 0) {
                            // Try fallback ID
                            it = sampleIdToHandle.find(fallbackId);
                            if (it != sampleIdToHandle.end()) {
                                sample.sampleHandle = it->second;
                            } else {
                                sample.sampleHandle = -1;  // Not found
                            }
                        } else {
                            sample.sampleHandle = -1;  // Not found
                        }
                        sample.fallbackHandle = -1;  // Clear the fallback field
                    }
                }

                std::cout << "Loaded " << sampleIdToHandle.size() << " OJM samples" << std::endl;
            }
        }
    } else if (isBMS) {
        BMSData bmsData;
        if (!BMSParser::parseFull(actualPath, bmsData)) {
            std::cerr << "Failed to parse BMS chart" << std::endl;
            return false;
        }
        beatmap = bmsData.beatmap;
        std::cout << "Loaded BMS chart: " << beatmap.keyCount << "K" << std::endl;

        // Apply mirror for 8K BMS if N+1 style and mirror are enabled
        // This moves scratch from left (lane 0) to right (lane 7)
        if (beatmap.keyCount == 8 && settings.n1Style && settings.mirror) {
            for (auto& note : beatmap.notes) {
                note.lane = beatmap.keyCount - 1 - note.lane;
            }
            std::cout << "Applied 8K mirror (scratch on right)" << std::endl;
        }

        // Load WAV files for key sounds
        fs::path bmsDir = fs::path(actualPath).parent_path();
        std::unordered_map<int, int> wavIdToHandle;

        for (const auto& [id, filename] : bmsData.wavDefs) {
            fs::path wavPath = bmsDir / filename;
            // Try different extensions if file not found
            if (!fs::exists(wavPath)) {
                std::string stem = wavPath.stem().string();
                fs::path parent = wavPath.parent_path();
                for (const auto& ext : {".wav", ".ogg", ".mp3"}) {
                    fs::path tryPath = parent / (stem + ext);
                    if (fs::exists(tryPath)) {
                        wavPath = tryPath;
                        break;
                    }
                }
            }
            if (fs::exists(wavPath)) {
                int handle = audio.loadSample(wavPath.string());
                if (handle >= 0) {
                    wavIdToHandle[id] = handle;
                }
            }
        }

        // Update notes with sample handles
        for (auto& note : beatmap.notes) {
            if (note.customIndex > 0) {
                auto it = wavIdToHandle.find(note.customIndex);
                if (it != wavIdToHandle.end()) {
                    note.sampleHandle = it->second;
                }
            }
        }

        // Update storyboard samples (BGM) with handles
        for (auto& sample : beatmap.storyboardSamples) {
            if (sample.sampleHandle > 0) {
                auto it = wavIdToHandle.find(sample.sampleHandle);
                if (it != wavIdToHandle.end()) {
                    sample.sampleHandle = it->second;
                } else {
                    sample.sampleHandle = -1;
                }
            }
        }

        // Load BGA images
        hasBga = false;
        isBmsBga = false;
        if (!bmsData.bgaEvents.empty()) {
            bmsBgaManager.init(renderer.getRenderer());
            bmsBgaManager.load(bmsData.bgaEvents, bmsData.bmpDefs, bmsData.directory);
            hasBga = true;
            isBmsBga = true;
            std::cout << "BMS BGA: " << bmsData.bgaEvents.size() << " events" << std::endl;
        }

        std::cout << "Loaded " << wavIdToHandle.size() << " WAV samples" << std::endl;
    } else if (isIIDX) {
        // Get difficulty index from version string
        int diffIdx = IIDXParser::SP_ANOTHER;  // Default
        for (int i = 0; i <= 10; i++) {
            if (beatmap.version == IIDXParser::getDifficultyName(i)) {
                diffIdx = i;
                break;
            }
        }
        if (!IIDXParser::parse(actualPath, beatmap, diffIdx)) {
            std::cerr << "Failed to parse IIDX chart" << std::endl;
            return false;
        }
        std::cout << "Loaded IIDX chart: " << beatmap.keyCount << "K" << std::endl;

        // Load S3P keysounds for IIDX
        std::filesystem::path chartPath(actualPath);
        std::string songId = chartPath.stem().string();  // e.g., "32083"
        std::filesystem::path s3pPath = chartPath.parent_path() / (songId + ".s3p");
        if (std::filesystem::exists(s3pPath)) {
            keySoundManager.loadS3PSamples(s3pPath.string());
        }
    } else if (isMalody) {
        if (!MalodyParser::parse(actualPath, beatmap)) {
            std::cerr << "Failed to parse Malody chart" << std::endl;
            return false;
        }
        std::cout << "Loaded Malody chart: " << beatmap.keyCount << "K" << std::endl;
    } else if (isMuSynx) {
        if (!MuSynxParser::parse(actualPath, beatmap)) {
            std::cerr << "Failed to parse MUSYNX chart" << std::endl;
            return false;
        }
        std::cout << "Loaded MUSYNX chart: " << beatmap.keyCount << "K" << std::endl;

        // Extract song name from chart filename for ACB lookup
        // e.g., "silverTown4T_easy.txt" -> "silverTown"
        std::filesystem::path chartPath(actualPath);
        std::string chartName = chartPath.stem().string();
        size_t keyPos = chartName.find("4T");
        if (keyPos == std::string::npos) keyPos = chartName.find("6T");
        std::string songName = (keyPos != std::string::npos) ? chartName.substr(0, keyPos) : chartName;

        // Look for ACB file in parent directory or MUSYNX StreamingAssets
        std::string acbName = "song_" + songName + ".acb";
        std::filesystem::path acbPath = chartPath.parent_path() / acbName;

        if (std::filesystem::exists(acbPath)) {
            // Extract and convert HCA to WAV in temp directory
            std::string tempDir = "Data/Tmp/musynx_" + songName;
            std::string bgmWav = tempDir + "/bgm.wav";

            // Check if already extracted (cache)
            if (std::filesystem::exists(bgmWav)) {
                beatmap.audioFilename = bgmWav;
                std::cout << "Using cached MUSYNX audio: " << bgmWav << std::endl;
            } else {
                std::cout << "Extracting MUSYNX audio from: " << acbPath << std::endl;
                if (AcbParser::extractAndConvert(acbPath.string(), tempDir)) {
                    beatmap.audioFilename = bgmWav;
                    std::cout << "MUSYNX audio extracted to: " << tempDir << std::endl;
                }
            }

            // Update note filenames to full WAV paths
            for (auto& note : beatmap.notes) {
                if (!note.filename.empty()) {
                    note.filename = tempDir + "/" + note.filename + ".wav";
                }
            }
        }
    } else {
        if (!OsuParser::parse(beatmapPath, beatmap)) {
            std::cerr << "Failed to parse beatmap" << std::endl;
            return false;
        }
        if (!OsuParser::isMania(beatmap)) {
            // Convert osu!standard to mania
            // Use auto-calculated key count (like osu! does)
            int targetKeyCount = BeatmapConverter::calculateKeyCount(beatmap);
            std::cout << "Auto key count: " << targetKeyCount << "K" << std::endl;
            std::cout << "Total objects: " << beatmap.totalObjectCount
                      << ", Sliders/Spinners: " << beatmap.endTimeObjectCount << std::endl;

            if (!BeatmapConverter::convert(beatmap, targetKeyCount)) {
                std::cerr << "Failed to convert beatmap to mania" << std::endl;
                return false;
            }
            std::cout << "Converted to " << targetKeyCount << "K mania" << std::endl;
        }
    }
    } // end if (!skipParsing)

    // Clear keysound cache before loading new keysounds
    keySoundManager.clear();
    audio.clearSamples();

    // Load S3P/2DX keysounds for IIDX (must be outside skipParsing block)
    bool isIIDXFile = path.size() > 2 && path.substr(path.size() - 2) == ".1";
    std::cout << "[S3P DEBUG] path = '" << path << "', isIIDXFile = " << isIIDXFile << std::endl;
    if (isIIDXFile) {
        std::filesystem::path chartPath(path);
        std::string songId = chartPath.stem().string();
        std::filesystem::path s3pPath = chartPath.parent_path() / (songId + ".s3p");
        std::filesystem::path twoDxPath = chartPath.parent_path() / (songId + ".2dx");

        // Try S3P first, then 2DX (skip _pre.2dx files)
        if (std::filesystem::exists(s3pPath)) {
            std::cout << "[IIDX] Loading S3P: " << s3pPath.string() << std::endl;
            keySoundManager.loadS3PSamples(s3pPath.string());
        } else if (std::filesystem::exists(twoDxPath) && songId.find("_pre") == std::string::npos) {
            std::cout << "[IIDX] Loading 2DX: " << twoDxPath.string() << std::endl;
            keySoundManager.load2DXSamples(twoDxPath.string());
        }
    }

    // Load OJM keysounds for O2Jam (must be outside skipParsing block)
    bool isOjnFile = OjnParser::isOjnFile(actualPath);
    if (isOjnFile) {
        std::string ojmPath = OjmParser::getOjmPath(actualPath);
        if (!ojmPath.empty()) {
            OjmInfo ojmInfo;
            if (OjmParser::parse(ojmPath, ojmInfo)) {
                std::cout << "Loaded OJM: " << ojmInfo.samples.size() << " samples" << std::endl;

                fs::path tempDir = fs::current_path() / "Data" / "Tmp" / "ojm";
                fs::create_directories(tempDir);

                std::unordered_map<int, int> sampleIdToHandle;
                for (auto& [id, sample] : ojmInfo.samples) {
                    std::string ext = sample.isOgg ? ".ogg" : ".wav";
                    fs::path tempFile = tempDir / (std::to_string(id) + ext);

                    std::ofstream out(tempFile, std::ios::binary);
                    if (out) {
                        out.write(reinterpret_cast<const char*>(sample.data.data()), sample.data.size());
                        out.close();

                        int handle = audio.loadSample(tempFile.string());
                        if (handle >= 0) {
                            sampleIdToHandle[id] = handle;
                        }
                    }
                }

                // Update notes with sample handles
                for (auto& note : beatmap.notes) {
                    if (note.customIndex > 0) {
                        auto it = sampleIdToHandle.find(note.customIndex);
                        if (it != sampleIdToHandle.end()) {
                            note.sampleHandle = it->second;
                        }
                    }
                }

                // Update storyboard samples (BGM) with sample handles
                for (auto& sample : beatmap.storyboardSamples) {
                    if (sample.sampleHandle > 0) {
                        int primaryId = sample.sampleHandle;
                        int fallbackId = sample.fallbackHandle;

                        auto it = sampleIdToHandle.find(primaryId);
                        if (it != sampleIdToHandle.end()) {
                            sample.sampleHandle = it->second;
                        } else if (fallbackId > 0) {
                            it = sampleIdToHandle.find(fallbackId);
                            if (it != sampleIdToHandle.end()) {
                                sample.sampleHandle = it->second;
                            } else {
                                sample.sampleHandle = -1;
                            }
                        } else {
                            sample.sampleHandle = -1;
                        }
                        sample.fallbackHandle = -1;
                    }
                }

                std::cout << "Loaded " << sampleIdToHandle.size() << " OJM samples" << std::endl;
            }
        }
    }

    // Load PAK keysounds for DJMAX Online PT files (must be outside skipParsing block)
    bool isPTFile = PTParser::isPTFile(path);
    if (isPTFile) {
        fs::path ptPath(path);
        std::string stemStr = ptPath.stem().string();
        size_t underscorePos = stemStr.find('_');
        std::string baseName = (underscorePos != std::string::npos) ? stemStr.substr(0, underscorePos) : stemStr;
        fs::path pakPath = ptPath.parent_path() / (baseName + ".pak");

        if (!fs::exists(pakPath)) {
            for (const auto& entry : fs::directory_iterator(ptPath.parent_path())) {
                if (entry.path().extension() == ".pak") {
                    pakPath = entry.path();
                    break;
                }
            }
        }

        if (fs::exists(pakPath)) {
            static PakExtractor pakExtractor;
            static bool keysLoaded = false;
            if (!keysLoaded) {
                keysLoaded = pakExtractor.loadKeys("D:\\work\\DJMax_Online\\Xip-Pak-Extractor-main\\keyFiles");
            }
            if (keysLoaded && pakExtractor.open(pakPath.string())) {
                fs::path tempDir = fs::current_path() / "Data" / "Tmp" / pakPath.stem().string();
                fs::create_directories(tempDir);

                for (const auto& fileEntry : pakExtractor.getFileList()) {
                    std::string ext = fs::path(fileEntry.filename).extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    bool isAudio = (ext == ".ogg" || ext == ".wav" || ext == ".mp3");
                    if (isAudio) {
                        std::vector<uint8_t> data;
                        if (pakExtractor.extractFile(fileEntry.filename, data)) {
                            fs::path outPath = tempDir / fs::path(fileEntry.filename).filename();
                            std::ofstream out(outPath, std::ios::binary);
                            if (out) {
                                out.write(reinterpret_cast<char*>(data.data()), data.size());
                            }
                        }
                    }
                }
                ptAudioDir = tempDir.string();
                pakExtractor.close();
            }
        }
    }

    // Load WAV keysounds for BMS (must be outside skipParsing block)
    bool isBMSFile = BMSParser::isBMSFile(actualPath);
    std::cout << "[BMS DEBUG] isBMSFile=" << isBMSFile << ", actualPath=" << actualPath << std::endl;
    if (isBMSFile) {
        fs::path bmsDir = fs::path(actualPath).parent_path();

        // Parse WAV definitions
        BMSData bmsData;
        if (BMSParser::parseFull(actualPath, bmsData)) {
            std::unordered_map<int, int> wavIdToHandle;

            for (const auto& [id, filename] : bmsData.wavDefs) {
                fs::path wavPath = bmsDir / filename;
                if (!fs::exists(wavPath)) {
                    std::string stem = wavPath.stem().string();
                    fs::path parent = wavPath.parent_path();
                    for (const auto& ext : {".wav", ".ogg", ".mp3"}) {
                        fs::path tryPath = parent / (stem + ext);
                        if (fs::exists(tryPath)) {
                            wavPath = tryPath;
                            break;
                        }
                    }
                }
                if (fs::exists(wavPath)) {
                    int handle = audio.loadSample(wavPath.string());
                    if (handle >= 0) {
                        wavIdToHandle[id] = handle;
                    }
                }
            }

            // Update notes with sample handles
            for (auto& note : beatmap.notes) {
                if (note.customIndex > 0) {
                    auto it = wavIdToHandle.find(note.customIndex);
                    if (it != wavIdToHandle.end()) {
                        note.sampleHandle = it->second;
                    }
                }
            }

            // Update storyboard samples (BGM) with handles
            for (auto& sample : beatmap.storyboardSamples) {
                if (sample.sampleHandle > 0) {
                    auto it = wavIdToHandle.find(sample.sampleHandle);
                    if (it != wavIdToHandle.end()) {
                        sample.sampleHandle = it->second;
                    } else {
                        sample.sampleHandle = -1;
                    }
                }
            }

            std::cout << "Loaded " << wavIdToHandle.size() << " BMS WAV samples" << std::endl;

            // Load BGA
            hasBga = false;
            isBmsBga = false;
            if (!bmsData.bgaEvents.empty()) {
                bmsBgaManager.init(renderer.getRenderer());
                bmsBgaManager.load(bmsData.bgaEvents, bmsData.bmpDefs, bmsData.directory);
                hasBga = true;
                isBmsBga = true;
                std::cout << "BMS BGA: " << bmsData.bgaEvents.size() << " events" << std::endl;
            }
        }
    }

    // Set renderer key count based on beatmap
    renderer.setKeyCount(beatmap.keyCount);

    // Set colors based on beatmap key count (keys are stored per key count)
    settings.setDefaultColors(beatmap.keyCount);

    // Extract base BPM from first uninherited timing point
    baseBPM = 120.0;  // Default BPM
    for (const auto& tp : beatmap.timingPoints) {
        if (tp.uninherited && tp.beatLength > 0) {
            baseBPM = 60000.0 / tp.beatLength;
            break;
        }
    }

    // Set HP drain rate from beatmap
    hpManager.setHPDrainRate(beatmap.hp);

    // Reset lane key states
    for (int i = 0; i < 10; i++) {
        laneKeyDown[i] = false;
    }

    std::filesystem::path osuPath(beatmapPath);

    // Set beatmap folder path for skin manager (beatmap-specific skins)
    if (!settings.ignoreBeatmapSkin) {
        skinManager.setBeatmapPath(osuPath.parent_path().u8string());
    } else {
        skinManager.clearBeatmapPath();
    }

    // Determine audio path
    std::string audioPath;
    if (isMuSynx && !beatmap.audioFilename.empty() &&
        beatmap.audioFilename.find("Data/Tmp") == 0) {
        // MUSYNX: use absolute path directly
        audioPath = beatmap.audioFilename;
    } else {
        std::filesystem::path fullAudioPath = osuPath.parent_path() / beatmap.audioFilename;
        audioPath = fullAudioPath.u8string();
    }

    // Load music (BASS handles real-time tempo change)
    hasBackgroundMusic = audio.loadMusic(audioPath, false);  // Don't loop for gameplay

    if (!hasBackgroundMusic) {
        // Some beatmaps (like piano keysound maps) have no background music
        // All audio is played through key sounds
        std::cerr << "No background music: " << audioPath << " (keysound-only map?)" << std::endl;
    }

    // Preload key sounds (unless ignored)
    if (!ptAudioDir.empty()) {
        keySoundManager.setBeatmapDirectory(ptAudioDir);
    } else {
        keySoundManager.setBeatmapDirectory(osuPath.parent_path().u8string());
    }
    if (!settings.ignoreBeatmapHitsounds) {
        keySoundManager.preloadKeySounds(beatmap.notes);
        // Sort storyboard samples by time to ensure correct playback order
        std::sort(beatmap.storyboardSamples.begin(), beatmap.storyboardSamples.end(),
            [](const StoryboardSample& a, const StoryboardSample& b) {
                return a.time < b.time;
            });
        keySoundManager.preloadStoryboardSamples(beatmap.storyboardSamples);
        // Warmup audio system to prevent cold start delay
        audio.warmupSamples();
    }

    // Load storyboard (unless ignored)
    storyboard.clear();
    if (!settings.disableStoryboard) {
        storyboard.setBeatmapDirectory(osuPath.parent_path().string());
        storyboard.loadFromOsu(beatmapPath);

        // Try to load .osb file with same name as beatmap folder
        std::string osbPath;
        for (const auto& entry : fs::directory_iterator(osuPath.parent_path())) {
            if (entry.path().extension() == ".osb") {
                osbPath = entry.path().string();
                break;
            }
        }
        if (!osbPath.empty()) {
            storyboard.loadFromOsb(osbPath);
        }

        storyboard.loadTextures(renderer.getRenderer());
    }

    // Initialize judgement system
    judgementSystem.init(settings.judgeMode, beatmap.od, settings.customOD,
                         settings.judgements, baseBPM, clockRate);

    if (!beatmap.notes.empty()) {
        totalNotes = 0;
        for (const auto& note : beatmap.notes) {
            // ScoreV1: each note (including hold notes) counts as 1 judgement
            totalNotes += 1;
            int64_t endTime = note.isHold ? note.endTime : note.time;
            if (endTime > totalTime) totalTime = endTime;
        }
        totalTime += 2000;

        // Calculate skip target time (first note - 2000ms)
        int64_t firstNoteTime = beatmap.notes[0].time;
        skipTargetTime = firstNoteTime - 2000;
        // Skip is available if there's enough blank time before first note
        // Condition: firstNoteTime - 2000 > 3000 (simplified from osu! formula)
        canSkip = (skipTargetTime > 3000);

        // Calculate star rating with clockRate and initialize PP calculator
        currentStarRating = calculateStarRating(beatmap.notes, beatmap.keyCount,
            static_cast<StarRatingVersion>(settings.starRatingVersion), clockRate);
        ppCalculator.init(totalNotes, currentStarRating);

        // Initialize next note index for each lane (for empty tap keysound)
        for (int i = 0; i < 10; i++) {
            laneNextNoteIndex[i] = -1;
        }
        for (size_t i = 0; i < beatmap.notes.size(); i++) {
            int lane = beatmap.notes[i].lane;
            if (lane >= 0 && lane < 10 && laneNextNoteIndex[lane] == -1) {
                laneNextNoteIndex[lane] = static_cast<int>(i);
            }
        }
    }
    return true;
}

void Game::startAsyncLoad(const std::string& path, bool isReplayMode) {
    // Cancel any existing loading
    cancelLoading();

    // Store current state to return to on cancel
    stateBeforeLoading = state;
    pendingReplayMode = isReplayMode;

    // Reset loading state
    loadingState = LoadingState::Idle;
    loadingProgress = 0.0f;
    loadingCancelled = false;
    pendingBeatmapPath = path;

    {
        std::lock_guard<std::mutex> lock(loadingMutex);
        loadingStatusText = "Preparing...";
    }

    // Switch to loading state
    state = GameState::Loading;

    // Start loading thread
    loadingThread = std::thread(&Game::loadBeatmapAsync, this, path);
}

void Game::cancelLoading() {
    loadingCancelled = true;
    // Don't block - let the thread finish on its own
    // Thread will be joined when state changes to Cancelled
}

void Game::loadBeatmapAsync(const std::string& path) {
    // Helper macro to check cancellation
    #define CHECK_CANCELLED() if (loadingCancelled) { loadingState = LoadingState::Cancelled; return; }
    #define SET_STATUS(text) { std::lock_guard<std::mutex> lock(loadingMutex); loadingStatusText = text; }

    loadingState = LoadingState::Parsing;
    loadingProgress = 0.0f;
    SET_STATUS("Parsing chart...");

    // Reset game state (thread-safe parts only)
    beatmapPath = path;
    std::string savedVersion = beatmap.version;  // Save version for IIDX
    std::cout << "[ASYNC DEBUG] savedVersion = '" << savedVersion << "'" << std::endl;
    beatmap = BeatmapInfo();
    beatmap.version = savedVersion;  // Restore version for IIDX

    // Calculate clockRate (for non-replay mode, replay mode sets it later)
    clockRate = 1.0;
    scoreMultiplier = 1.0;
    if (!pendingReplayMode) {
        if (settings.doubleTimeEnabled || settings.nightcoreEnabled) {
            clockRate = 1.5;
        } else if (settings.halfTimeEnabled) {
            clockRate = 0.75;
            scoreMultiplier = 0.5;
        }
    }

    CHECK_CANCELLED();

    // Check for O2Jam difficulty suffix
    OjnDifficulty ojnDifficulty = OjnDifficulty::Hard;
    std::string actualPath = path;
    size_t colonPos = path.rfind(':');
    if (colonPos != std::string::npos && colonPos > 2) {
        size_t colonPos2 = path.rfind(':', colonPos - 1);
        if (colonPos2 != std::string::npos && colonPos2 > 2) {
            std::string diffStr = path.substr(colonPos2 + 1, colonPos - colonPos2 - 1);
            if (diffStr == "0") ojnDifficulty = OjnDifficulty::Easy;
            else if (diffStr == "1") ojnDifficulty = OjnDifficulty::Normal;
            else if (diffStr == "2") ojnDifficulty = OjnDifficulty::Hard;
            actualPath = path.substr(0, colonPos2);
        }
    }

    // Check file type
    bool isDJMax = DJMaxParser::isDJMaxChart(actualPath);
    bool isPT = PTParser::isPTFile(actualPath);
    bool isOjn = OjnParser::isOjnFile(actualPath);
    bool isBMS = BMSParser::isBMSFile(actualPath);
    bool isIIDX = actualPath.size() > 2 && actualPath.substr(actualPath.size() - 2) == ".1";
    bool isMalody = actualPath.size() > 3 && actualPath.substr(actualPath.size() - 3) == ".mc";
    bool isMuSynx = actualPath.size() > 4 && actualPath.substr(actualPath.size() - 4) == ".txt" &&
                   (actualPath.find("4T_") != std::string::npos || actualPath.find("6T_") != std::string::npos);
    std::string ptAudioDir;

    loadingProgress = 0.05f;
    CHECK_CANCELLED();

    // Parse chart based on file type
    bool parseSuccess = false;
    if (isDJMax) {
        SET_STATUS("Parsing DJMAX chart...");
        parseSuccess = DJMaxParser::parse(actualPath, beatmap);
    } else if (isPT) {
        SET_STATUS("Parsing PT chart...");
        parseSuccess = PTParser::parse(actualPath, beatmap);
    } else if (isOjn) {
        SET_STATUS("Parsing O2Jam chart...");
        parseSuccess = OjnParser::parse(actualPath, beatmap, ojnDifficulty);
    } else if (isBMS) {
        SET_STATUS("Parsing BMS chart...");
        parseSuccess = BMSParser::parse(actualPath, beatmap);
    } else if (isIIDX) {
        SET_STATUS("Parsing IIDX chart...");
        // Get difficulty index from version string
        int diffIdx = IIDXParser::SP_ANOTHER;  // Default
        std::cout << "[IIDX DEBUG] beatmap.version = '" << beatmap.version << "'" << std::endl;
        for (int i = 0; i <= 10; i++) {
            const char* diffName = IIDXParser::getDifficultyName(i);
            std::cout << "[IIDX DEBUG] Comparing with [" << i << "] '" << diffName << "'" << std::endl;
            if (beatmap.version == diffName) {
                diffIdx = i;
                std::cout << "[IIDX DEBUG] MATCH! diffIdx = " << diffIdx << std::endl;
                break;
            }
        }
        std::cout << "[IIDX DEBUG] Final diffIdx = " << diffIdx << std::endl;
        parseSuccess = IIDXParser::parse(actualPath, beatmap, diffIdx);
    } else if (isMalody) {
        SET_STATUS("Parsing Malody chart...");
        parseSuccess = MalodyParser::parse(actualPath, beatmap);
    } else if (isMuSynx) {
        SET_STATUS("Parsing MUSYNX chart...");
        parseSuccess = MuSynxParser::parse(actualPath, beatmap);
    } else {
        SET_STATUS("Parsing osu! chart...");
        parseSuccess = OsuParser::parse(path, beatmap);
        if (parseSuccess && !OsuParser::isMania(beatmap)) {
            SET_STATUS("Converting to mania...");
            int targetKeyCount = BeatmapConverter::calculateKeyCount(beatmap);
            parseSuccess = BeatmapConverter::convert(beatmap, targetKeyCount);
        }
    }

    if (!parseSuccess) {
        SET_STATUS("Failed to parse chart");
        loadingState = LoadingState::Failed;
        return;
    }

    loadingProgress = 0.2f;
    CHECK_CANCELLED();

    // Extract audio for formats that need it
    loadingState = LoadingState::ExtractingAudio;

    if (isPT) {
        SET_STATUS("Extracting PAK audio...");
        fs::path ptPath(actualPath);
        std::string stemStr = ptPath.stem().string();
        size_t underscorePos = stemStr.find('_');
        std::string baseName = (underscorePos != std::string::npos) ? stemStr.substr(0, underscorePos) : stemStr;
        fs::path pakPath = ptPath.parent_path() / (baseName + ".pak");

        if (!fs::exists(pakPath)) {
            for (const auto& entry : fs::directory_iterator(ptPath.parent_path())) {
                if (entry.path().extension() == ".pak") {
                    pakPath = entry.path();
                    break;
                }
            }
        }

        if (fs::exists(pakPath)) {
            static PakExtractor pakExtractor;
            static bool keysLoaded = false;
            if (!keysLoaded) {
                keysLoaded = pakExtractor.loadKeys("D:\\work\\DJMax_Online\\Xip-Pak-Extractor-main\\keyFiles");
            }
            if (keysLoaded && pakExtractor.open(pakPath.string())) {
                fs::path tempDir = fs::current_path() / "Data" / "Tmp" / pakPath.stem().string();
                fs::create_directories(tempDir);
                for (const auto& fileEntry : pakExtractor.getFileList()) {
                    CHECK_CANCELLED();
                    std::string ext = fs::path(fileEntry.filename).extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    bool isAudio = (ext == ".ogg" || ext == ".wav" || ext == ".mp3");
                    if (isAudio) {
                        std::vector<uint8_t> data;
                        if (pakExtractor.extractFile(fileEntry.filename, data)) {
                            fs::path outPath = tempDir / fs::path(fileEntry.filename).filename();
                            std::ofstream out(outPath, std::ios::binary);
                            if (out) {
                                out.write(reinterpret_cast<char*>(data.data()), data.size());
                            }
                        }
                    }
                }
                ptAudioDir = tempDir.string();
                pakExtractor.close();
            }
        }
    }

    loadingProgress = 0.35f;
    CHECK_CANCELLED();

    // MUSYNX ACB extraction
    if (isMuSynx) {
        SET_STATUS("Extracting MUSYNX audio...");
        try {
            std::filesystem::path chartPath(actualPath);
            std::string chartName = chartPath.stem().string();
            size_t keyPos = chartName.find("4T");
            if (keyPos == std::string::npos) keyPos = chartName.find("6T");
            std::string songName = (keyPos != std::string::npos) ? chartName.substr(0, keyPos) : chartName;

            std::string acbName = "song_" + songName + ".acb";
            std::filesystem::path acbPath = chartPath.parent_path() / acbName;

            if (std::filesystem::exists(acbPath)) {
                std::string tempDir = "Data/Tmp/musynx_" + songName;
                std::string bgmWav = tempDir + "/bgm.wav";

                if (std::filesystem::exists(bgmWav)) {
                    beatmap.audioFilename = bgmWav;
                } else {
                    // Extract cue names from ACB file (in correct HCA order)
                    auto acbCues = AcbParser::extractCueNames(acbPath.string());
                    std::vector<std::string> cueNames;
                    for (const auto& cue : acbCues) {
                        cueNames.push_back(cue.name);
                    }
                    std::cerr << "[MUSYNX] Extracted " << cueNames.size() << " cue names from ACB" << std::endl;
                    AcbParser::extractAndConvert(acbPath.string(), tempDir, cueNames);
                    std::cerr << "[MUSYNX] Audio extraction complete" << std::endl;
                    beatmap.audioFilename = bgmWav;
                }

                for (auto& note : beatmap.notes) {
                    if (!note.filename.empty()) {
                        note.filename = tempDir + "/" + note.filename + ".wav";
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[MUSYNX] Exception during ACB extraction: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[MUSYNX] Unknown exception during ACB extraction" << std::endl;
        }
    }

    loadingProgress = 0.5f;
    CHECK_CANCELLED();

    // Load audio
    loadingState = LoadingState::LoadingAudio;
    SET_STATUS("Loading audio...");

    std::filesystem::path osuPath(path);
    std::string audioPath;
    if (isMuSynx && !beatmap.audioFilename.empty() &&
        beatmap.audioFilename.find("Data/Tmp") == 0) {
        audioPath = beatmap.audioFilename;
    } else if (!ptAudioDir.empty()) {
        audioPath = ptAudioDir + "/" + beatmap.audioFilename;
    } else {
        std::filesystem::path fullAudioPath = osuPath.parent_path() / beatmap.audioFilename;
        audioPath = fullAudioPath.u8string();
    }

    loadingProgress = 0.6f;
    CHECK_CANCELLED();

    // Load keysounds
    loadingState = LoadingState::LoadingKeysounds;
    SET_STATUS("Loading keysounds...");

    if (!ptAudioDir.empty()) {
        keySoundManager.setBeatmapDirectory(ptAudioDir);
    } else {
        keySoundManager.setBeatmapDirectory(osuPath.parent_path().u8string());
    }

    loadingProgress = 0.8f;
    CHECK_CANCELLED();

    // Store data for main thread to finalize
    loadingState = LoadingState::LoadingAssets;
    SET_STATUS("Finalizing...");

    loadingProgress = 0.95f;

    // Mark as completed - main thread will do SDL operations
    loadingState = LoadingState::Completed;
    loadingProgress = 1.0f;
    SET_STATUS("Loading complete");

    #undef CHECK_CANCELLED
    #undef SET_STATUS
}

void Game::cleanupTempDir() {
    // Clean up BGA textures only (called when switching songs)
    for (auto& [name, tex] : bgaTextures) {
        if (tex) SDL_DestroyTexture(tex);
    }
    bgaTextures.clear();
    hasBga = false;
}

void Game::cleanupTempFiles() {
    // Clean up temp files (called on startup and exit only)
    fs::path tempDir = fs::current_path() / "Data" / "Tmp";
    if (fs::exists(tempDir)) {
        std::error_code ec;
        fs::remove_all(tempDir, ec);
        if (!ec) {
            std::cout << "Cleaned up temp directory: " << tempDir.string() << std::endl;
        }
    }
}

void Game::updateBga(int64_t currentTime) {
    if (!hasBga || bgaData.timeline.empty()) return;

    // Don't update BGA if time is negative (before song starts)
    if (currentTime < 0) return;

    static bool updateDebugOnce = false;
    if (!updateDebugOnce) {
        std::cout << "updateBga: First call, timeline size=" << bgaData.timeline.size() << std::endl;
        updateDebugOnce = true;
    }

    // Convert time to frame number (30fps, scaled by clockRate)
    uint32_t currentFrame = (uint32_t)(currentTime * 30 * clockRate / 1000);

    // Process timeline entries up to current frame
    while (currentBgaEntry < bgaData.timeline.size()) {
        const auto& entry = bgaData.timeline[currentBgaEntry];
        if (entry.frameTime > currentFrame) break;

        std::cout << "updateBga: Processing entry " << currentBgaEntry
                  << ", layerId=" << entry.layerId
                  << ", frameTime=" << entry.frameTime
                  << ", vce=" << entry.vceFile << std::endl;

        // Load VCE for this layer
        fs::path vcePath = fs::path(bgaData.bgaDir) / entry.vceFile;
        if (fs::exists(vcePath)) {
            BgaLayer& layer = bgaData.layers[entry.layerId];
            if (BgaParser::parseVce(vcePath.string(), layer.effect)) {
                layer.active = true;
                layer.currentVce = entry.vceFile;
                layer.startFrame = entry.frameTime;
                layer.currentKfIndex = 0;
                layer.currentImageIndex = 0;
                layer.frameIndex = 0;
                memset(layer.values, 0, sizeof(layer.values));

                // Debug: show first keyframe info
                if (!layer.effect.keyframes.empty()) {
                    const auto& kf0 = layer.effect.keyframes[0];
                    std::cout << "  Layer " << entry.layerId << " first kf: frame=" << kf0.frame
                              << " flags=" << kf0.flags << std::endl;
                }
            }
        }
        currentBgaEntry++;
        std::cout << "updateBga: Entry " << (currentBgaEntry-1) << " done" << std::endl;
    }

    static bool updateDebug2 = false;
    if (!updateDebug2) {
        std::cout << "updateBga: After loading VCEs, layers=" << bgaData.layers.size() << std::endl;
        updateDebug2 = true;
    }

    // Update each active layer
    static int frameDebugCount = 0;
    for (auto& [layerId, layer] : bgaData.layers) {
        if (!layer.active || layer.effect.keyframes.empty()) continue;
        if (layer.currentKfIndex < 0) continue;  // Animation finished

        uint32_t relativeFrame = currentFrame - layer.startFrame;

        // Get current keyframe
        if (layer.currentKfIndex >= (int)layer.effect.keyframes.size()) {
            layer.active = false;
            continue;
        }

        const auto& kf = layer.effect.keyframes[layer.currentKfIndex];
        int frameDiff = (int)relativeFrame - (int)kf.frame;

        if (frameDiff >= 0) {
            layer.currentImageIndex = kf.imageIndex;

            if (kf.flags != 0) {
                // Delta mode: accumulate values each frame
                for (int i = 0; i < 18; i++) {
                    layer.values[i] += kf.values[i];
                }
            } else {
                // Absolute mode: copy values directly
                for (int i = 0; i < 18; i++) {
                    layer.values[i] = kf.values[i];
                }
            }

            // Check if we should advance to next keyframe
            if (layer.currentKfIndex + 1 < (int)layer.effect.keyframes.size()) {
                const auto& nextKf = layer.effect.keyframes[layer.currentKfIndex + 1];
                if (nextKf.frame == relativeFrame) {
                    layer.currentKfIndex++;
                }
            } else {
                layer.currentKfIndex = -1;  // Animation finished
            }
        }

        // Deactivate if past total frames
        if (relativeFrame > layer.effect.totalFrames) {
            layer.active = false;
        }
    }

    static bool updateDebug3 = false;
    if (!updateDebug3) {
        std::cout << "updateBga: Finished updating layers" << std::endl;
        updateDebug3 = true;
    }
}

void Game::renderBga() {
    if (!hasBga) return;

    static bool renderDebugOnce = false;
    if (!renderDebugOnce) {
        std::cout << "renderBga: First call, layers count=" << bgaData.layers.size() << std::endl;
        renderDebugOnce = true;
    }

    SDL_Renderer* sdlRenderer = renderer.getRenderer();
    int windowW = 1280, windowH = 720;

    // Collect and sort layers by ID
    std::vector<int> layerIds;
    for (const auto& [id, layer] : bgaData.layers) {
        if (layer.active) layerIds.push_back(id);
    }
    std::sort(layerIds.begin(), layerIds.end());

    static bool renderDebug2 = false;
    if (!renderDebug2 && !layerIds.empty()) {
        std::cout << "renderBga: Active layers: " << layerIds.size() << std::endl;
        for (int layerId : layerIds) {
            const BgaLayer& layer = bgaData.layers[layerId];
            std::cout << "  Layer " << layerId << ": imgIdx=" << layer.currentImageIndex
                      << " values[0-5]=" << layer.values[0] << "," << layer.values[1]
                      << "," << layer.values[4] << "," << layer.values[5] << std::endl;
        }
        renderDebug2 = true;
    }

    // Render each layer
    for (int layerId : layerIds) {
        const BgaLayer& layer = bgaData.layers[layerId];
        if (!layer.active || layer.currentImageIndex < 0) continue;
        if (layer.currentImageIndex >= (int)layer.effect.images.size()) continue;

        const std::string& imgFile = layer.effect.images[layer.currentImageIndex].filename;
        fs::path imgPath = fs::path(bgaData.bgaDir) / imgFile;

        // Load texture if not cached
        if (bgaTextures.find(imgFile) == bgaTextures.end()) {
            int w, h, channels;
            unsigned char* data = stbi_load(imgPath.string().c_str(), &w, &h, &channels, 4);
            if (data) {
                SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, data, w * 4);
                if (surface) {
                    bgaTextures[imgFile] = SDL_CreateTextureFromSurface(sdlRenderer, surface);
                    SDL_DestroySurface(surface);
                }
                stbi_image_free(data);
            }
        }

        SDL_Texture* tex = bgaTextures[imgFile];
        if (!tex) continue;

        // Get texture size
        float texW, texH;
        SDL_GetTextureSize(tex, &texW, &texH);

        // Get transform values from layer
        float scaleX = layer.values[4] > 0 ? layer.values[4] : 1.0f;
        float scaleY = layer.values[5] > 0 ? layer.values[5] : 1.0f;
        float alpha = layer.values[17] > 0 ? layer.values[17] : 1.0f;
        float posX = layer.values[0];
        float posY = layer.values[1];

        // Use original size with scale
        float destW = texW * scaleX;
        float destH = texH * scaleY;
        // Center on screen and apply position offset
        float destX = (windowW - destW) / 2 + posX;
        float destY = (windowH - destH) / 2 + posY;

        SDL_FRect destRect = {destX, destY, destW, destH};

        // Apply alpha
        SDL_SetTextureAlphaMod(tex, (Uint8)(alpha * 255));
        SDL_RenderTexture(sdlRenderer, tex, nullptr, &destRect);
    }
}

void Game::run() {
    running = true;
    state = GameState::Menu;
    lastFrameTime = SDL_GetTicks();

    Uint64 perfFreq = SDL_GetPerformanceFrequency();

    while (running) {
        Uint64 frameStartPerf = SDL_GetPerformanceCounter();
        int64_t frameStart = SDL_GetTicks();

        // Performance monitoring - Input
        Uint64 t1 = SDL_GetPerformanceCounter();
        handleInput();
        Uint64 t2 = SDL_GetPerformanceCounter();
        perfInput = (double)(t2 - t1) * 1000.0 / perfFreq;

        // Performance monitoring - Update
        if (state == GameState::Playing) {
            update();
        } else if (state == GameState::Dead) {
            // Update slowdown effect (1.0 -> 0.0 over ~1 second)
            if (deathSlowdown > 0.0f) {
                deathSlowdown -= 0.016f;  // ~60fps, 1 second to stop
                if (deathSlowdown < 0.0f) deathSlowdown = 0.0f;
            }
        } else if (state == GameState::Loading) {
            // Check if loading completed
            LoadingState ls = loadingState.load();
            if (ls == LoadingState::Completed) {
                // Finalize loading on main thread (SDL operations)
                if (loadingThread.joinable()) {
                    loadingThread.join();
                }
                // Call the synchronous loadBeatmap to do SDL operations (skip parsing since async already did it)
                if (loadBeatmap(pendingBeatmapPath, true)) {
                    if (pendingReplayMode) {
                        // Setup replay mode
                        replayMode = true;
                        autoPlay = (replayInfo.mods & 2048) != 0;
                        // Apply replay mods (don't modify settings, use local variables)
                        bool replayHT = (replayInfo.mods & 256) != 0;
                        bool replayDT = (replayInfo.mods & 64) != 0;
                        bool replayNC = (replayInfo.mods & 512) != 0;
                        if (replayNC) replayDT = true;

                        if (replayDT) {
                            clockRate = 1.5;
                        } else if (replayHT) {
                            clockRate = 0.75;
                        } else {
                            clockRate = 1.0;
                        }
                        audio.setPlaybackRate(clockRate);
                        audio.setChangePitch(replayNC);

                        currentStarRating = calculateStarRating(beatmap.notes, beatmap.keyCount,
                            static_cast<StarRatingVersion>(settings.starRatingVersion), clockRate);
                        ppCalculator.init(totalNotes, currentStarRating);
                        judgementSystem.init(settings.judgeMode, beatmap.od, settings.customOD,
                                             settings.judgements, baseBPM, clockRate);
                        currentReplayFrame = 0;
                        // Set window title for replay mode
                        std::string title = "[REPLAY MODE] Mania Player - " + beatmap.artist + " " + beatmap.title +
                                           " [" + beatmap.version + "](" + beatmap.creator + ") Player:" + replayInfo.playerName;
                        renderer.setWindowTitle(title);
                    } else {
                        // Normal play mode - apply settings
                        replayMode = false;
                        autoPlay = settings.autoPlayEnabled;
                    }
                    state = GameState::Playing;
                    musicStarted = false;
                    startTime = SDL_GetTicks();
                    std::cout << "[DEBUG] State set to Playing, hasBackgroundMusic=" << hasBackgroundMusic << std::endl;
                    SDL_FlushEvent(SDL_EVENT_KEY_DOWN);
                    SDL_FlushEvent(SDL_EVENT_KEY_UP);
                } else {
                    state = stateBeforeLoading;
                }
                loadingState = LoadingState::Idle;
                pendingReplayMode = false;
            } else if (ls == LoadingState::Failed || ls == LoadingState::Cancelled) {
                if (loadingThread.joinable()) {
                    loadingThread.join();
                }
                state = stateBeforeLoading;
                loadingState = LoadingState::Idle;
                pendingReplayMode = false;
            }
        }
        Uint64 t3 = SDL_GetPerformanceCounter();
        perfUpdate = (double)(t3 - t2) * 1000.0 / perfFreq;

        // Performance monitoring - Draw
        render();
        Uint64 t4 = SDL_GetPerformanceCounter();
        perfDraw = (double)(t4 - t3) * 1000.0 / perfFreq;

        // High-precision frame timing
        double targetFrameTimeUs = targetFrameDelay * 1000.0;  // Convert ms to us
        while (true) {
            Uint64 now = SDL_GetPerformanceCounter();
            double elapsedUs = (double)(now - frameStartPerf) * 1000000.0 / perfFreq;
            if (elapsedUs >= targetFrameTimeUs) break;

            // If more than 2ms remaining, use SDL_Delay to save CPU
            double remainingMs = (targetFrameTimeUs - elapsedUs) / 1000.0;
            if (remainingMs > 2.0) {
                SDL_Delay(1);
            }
            // Otherwise busy-wait for precision
        }
    }
}

void Game::handleInput() {
    mouseClicked = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) {
            running = false;
        }
        else if (e.type == SDL_EVENT_MOUSE_MOTION) {
            float fx, fy;
            SDL_RenderCoordinatesFromWindow(renderer.getRenderer(), e.motion.x, e.motion.y, &fx, &fy);
            mouseX = (int)fx;
            mouseY = (int)fy;
            // Handle drag scrolling in song select
            if (state == GameState::SongSelect && songSelectDragging) {
                songSelectScroll = songSelectDragStartScroll + (songSelectDragStartY - mouseY);
                if (songSelectScroll < 0) songSelectScroll = 0;
            }
        }
        else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (e.button.button == SDL_BUTTON_LEFT) {
                // In song select, don't set mouseClicked on down - wait for up
                if (state != GameState::SongSelect) {
                    mouseClicked = true;
                }
                mouseDown = true;
                // Start drag scrolling in song select
                if (state == GameState::SongSelect) {
                    songSelectDragging = true;
                    songSelectDragStartY = mouseY;
                    songSelectDragStartScroll = songSelectScroll;
                }
            }
        }
        else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            if (e.button.button == SDL_BUTTON_LEFT) {
                mouseDown = false;
                // In song select, only trigger click if not dragging
                if (state == GameState::SongSelect && songSelectDragging) {
                    bool isDragging = std::abs(mouseY - songSelectDragStartY) > 5;
                    if (!isDragging) {
                        mouseClicked = true;
                    }
                }
                songSelectDragging = false;
            }
        }
        else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            if (state == GameState::SongSelect) {
                songSelectScroll -= e.wheel.y * 60;  // 60 pixels per scroll
                // Clamp will be handled in render loop where we know the total height
                if (songSelectScroll < 0) songSelectScroll = 0;
            }
            else if (state == GameState::Settings) {
                // Check if mouse is in content area
                float winX = (1280 - 800) / 2;
                float winY = (720 - 500) / 2;
                float contentX = winX + 180;
                float contentY = winY + 60;
                float contentW = 600;
                float contentH = 390;
                if (mouseX >= contentX && mouseX <= contentX + contentW &&
                    mouseY >= contentY && mouseY <= contentY + contentH) {
                    settingsScroll -= e.wheel.y * 40;
                    if (settingsScroll < 0) settingsScroll = 0;
                }
            }
        }
        else if (e.type == SDL_EVENT_TEXT_INPUT) {
            if (state == GameState::Settings && editingUsername) {
                // Insert typed character at cursor position (limit to 32 chars)
                if (settings.username.length() < 32) {
                    settings.username.insert(settingsCursorPos, e.text.text);
                    settingsCursorPos += strlen(e.text.text);
                }
            }
            if (state == GameState::Settings && editingScrollSpeed) {
                // Append typed character to scroll speed (only digits, limit to 4 chars)
                char c = e.text.text[0];
                if (c >= '0' && c <= '9' && scrollSpeedInput.length() < 4) {
                    scrollSpeedInput += c;
                }
            }
            // Replay Factory text input
            if (state == GameState::ReplayFactory) {
                std::string* editStr = nullptr;
                size_t maxLen = 0;
                if (editingPlayerName) { editStr = &factoryReplayInfo.playerName; maxLen = 32; }
                else if (editingTimestamp) { editStr = &factoryTimestampStr; maxLen = 32; }
                else if (editingJudgements) { editStr = &factoryJudgementsStr; maxLen = 64; }
                else if (editingScore) { editStr = &factoryScoreStr; maxLen = 16; }
                else if (editingCombo) { editStr = &factoryComboStr; maxLen = 8; }
                else if (editingBlockHeight) { editStr = &blockHeightInput; maxLen = 4; }
                else if (editingVideoWidth) { editStr = &videoWidthInput; maxLen = 4; }
                else if (editingVideoHeight) { editStr = &videoHeightInput; maxLen = 4; }
                else if (editingVideoFPS) { editStr = &videoFPSInput; maxLen = 3; }

                if (editStr && editStr->length() < maxLen) {
                    editStr->insert(cursorPos, e.text.text);
                    cursorPos += strlen(e.text.text);
                }
            }
        }
        else if (e.type == SDL_EVENT_KEY_DOWN) {
            if (state == GameState::Menu) {
                if (e.key.key == SDLK_ESCAPE) {
                    running = false;
                }
            }
            else if (state == GameState::Loading) {
                if (e.key.key == SDLK_ESCAPE) {
                    cancelLoading();
                    state = stateBeforeLoading;
                }
            }
            else if (state == GameState::SongSelect) {
                if (e.key.key == SDLK_ESCAPE) {
                    stopPreviewMusic();
                    if (currentBgTexture) {
                        SDL_DestroyTexture(currentBgTexture);
                        currentBgTexture = nullptr;
                    }
                    state = GameState::Menu;
                }
                else if (e.key.key == SDLK_UP) {
                    if (selectedDifficultyIndex > 0) {
                        // Move up within difficulty list
                        selectedDifficultyIndex--;
                        loadSongBackground(selectedSongIndex, selectedDifficultyIndex);
                        playPreviewMusic(selectedSongIndex, selectedDifficultyIndex);
                        songSelectNeedAutoScroll = true;
                    } else if (selectedSongIndex > 0) {
                        // Move to previous song
                        selectedSongIndex--;
                        selectedDifficultyIndex = 0;
                        loadSongBackground(selectedSongIndex, selectedDifficultyIndex);
                        playPreviewMusic(selectedSongIndex, selectedDifficultyIndex);
                        songSelectNeedAutoScroll = true;
                    }
                }
                else if (e.key.key == SDLK_DOWN) {
                    int numDifficulties = (int)songList[selectedSongIndex].beatmapFiles.size();
                    if (selectedDifficultyIndex < numDifficulties - 1) {
                        // Move down within difficulty list
                        selectedDifficultyIndex++;
                        loadSongBackground(selectedSongIndex, selectedDifficultyIndex);
                        playPreviewMusic(selectedSongIndex, selectedDifficultyIndex);
                        songSelectNeedAutoScroll = true;
                    } else if (selectedSongIndex < (int)songList.size() - 1) {
                        // Move to next song
                        selectedSongIndex++;
                        selectedDifficultyIndex = 0;
                        loadSongBackground(selectedSongIndex, selectedDifficultyIndex);
                        playPreviewMusic(selectedSongIndex, selectedDifficultyIndex);
                        songSelectNeedAutoScroll = true;
                    }
                }
                else if (e.key.key == SDLK_RETURN) {
                    if (!songList.empty() && !songSelectTransition) {
                        std::string path = songList[selectedSongIndex].beatmapFiles[selectedDifficultyIndex];
                        std::cout << "[SONG SELECT DEBUG] selectedDifficultyIndex=" << selectedDifficultyIndex
                                  << ", difficulties.size()=" << songList[selectedSongIndex].difficulties.size() << std::endl;
                        // Set version and hash for replay export
                        if (selectedDifficultyIndex < (int)songList[selectedSongIndex].difficulties.size()) {
                            beatmap.version = songList[selectedSongIndex].difficulties[selectedDifficultyIndex].version;
                            beatmap.beatmapHash = songList[selectedSongIndex].difficulties[selectedDifficultyIndex].hash;
                            std::cout << "[SONG SELECT DEBUG] Set beatmap.version = '" << beatmap.version << "'" << std::endl;
                        }
                        stopPreviewMusic();
                        startAsyncLoad(path);
                    }
                }
                else if (e.key.key == SDLK_F5) {
                    // Force rebuild index (like Clear Index)
                    std::string indexDir = SongIndex::getIndexDir();
                    if (std::filesystem::exists(indexDir)) {
                        std::filesystem::remove_all(indexDir);
                    }
                    songList.clear();
                    scanSongsFolder();
                    selectedSongIndex = 0;
                    selectedDifficultyIndex = 0;
                    songSelectScroll = 0.0f;
                    if (!songList.empty()) {
                        loadSongBackground(0, 0);
                        playPreviewMusic(0, 0);
                    }
                }
            }
            else if (state == GameState::Settings) {
                if (editingUsername) {
                    // Handle text input for username
                    if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_RETURN) {
                        editingUsername = false;
                    } else if (e.key.key == SDLK_BACKSPACE) {
                        if (settingsCursorPos > 0 && !settings.username.empty()) {
                            settings.username.erase(settingsCursorPos - 1, 1);
                            settingsCursorPos--;
                        }
                    } else if (e.key.key == SDLK_DELETE) {
                        if (settingsCursorPos < (int)settings.username.length()) {
                            settings.username.erase(settingsCursorPos, 1);
                        }
                    } else if (e.key.key == SDLK_LEFT) {
                        if (settingsCursorPos > 0) settingsCursorPos--;
                    } else if (e.key.key == SDLK_RIGHT) {
                        if (settingsCursorPos < (int)settings.username.length()) settingsCursorPos++;
                    }
                } else if (editingScrollSpeed) {
                    // Handle text input for scroll speed
                    if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_RETURN) {
                        editingScrollSpeed = false;
                        // Apply the value
                        if (!scrollSpeedInput.empty()) {
                            try {
                                int newSpeed = std::stoi(scrollSpeedInput);
                                if (settings.unlimitedSpeed) {
                                    settings.scrollSpeed = std::max(1, newSpeed);
                                } else {
                                    settings.scrollSpeed = std::max(1, std::min(40, newSpeed));
                                }
                            } catch (...) {}
                        }
                        scrollSpeedInput = std::to_string(settings.scrollSpeed);
                    } else if (e.key.key == SDLK_BACKSPACE) {
                        if (!scrollSpeedInput.empty()) {
                            scrollSpeedInput.pop_back();
                        }
                    }
                } else {
                    if (e.key.key == SDLK_ESCAPE) {
                        saveConfig();
                        state = GameState::Menu;
                    }
                }
            }
            else if (state == GameState::ReplayFactory) {
                // Get current editing string
                std::string* editStr = nullptr;
                if (editingPlayerName) editStr = &factoryReplayInfo.playerName;
                else if (editingTimestamp) editStr = &factoryTimestampStr;
                else if (editingJudgements) editStr = &factoryJudgementsStr;
                else if (editingScore) editStr = &factoryScoreStr;
                else if (editingCombo) editStr = &factoryComboStr;
                else if (editingBlockHeight) editStr = &blockHeightInput;
                else if (editingVideoWidth) editStr = &videoWidthInput;
                else if (editingVideoHeight) editStr = &videoHeightInput;
                else if (editingVideoFPS) editStr = &videoFPSInput;

                // Handle backspace - delete character before cursor
                if (e.key.key == SDLK_BACKSPACE) {
                    if (editStr && cursorPos > 0) {
                        editStr->erase(cursorPos - 1, 1);
                        cursorPos--;
                    }
                }
                // Handle Delete key - delete character at cursor
                else if (e.key.key == SDLK_DELETE) {
                    if (editStr && cursorPos < (int)editStr->length()) {
                        editStr->erase(cursorPos, 1);
                    }
                }
                // Handle left arrow - move cursor left
                else if (e.key.key == SDLK_LEFT) {
                    if (cursorPos > 0) cursorPos--;
                }
                // Handle right arrow - move cursor right
                else if (e.key.key == SDLK_RIGHT) {
                    if (editStr && cursorPos < (int)editStr->length()) cursorPos++;
                }
                // Handle Home key - move cursor to start
                else if (e.key.key == SDLK_HOME) {
                    cursorPos = 0;
                }
                // Handle End key - move cursor to end
                else if (e.key.key == SDLK_END) {
                    if (editStr) cursorPos = (int)editStr->length();
                }
                else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_ESCAPE) {
                    // Finish editing and save values
                    if (editingTimestamp) {
                        // Parse timestamp string (local time) to .NET DateTime.Ticks (UTC)
                        int year, month, day, hour, min, sec;
                        int parsed = sscanf(factoryTimestampStr.c_str(), "%d/%d/%d %d:%d:%d",
                                   &year, &month, &day, &hour, &min, &sec);
                        std::cout << "Parsed " << parsed << " values: " << year << "/" << month << "/" << day
                                  << " " << hour << ":" << min << ":" << sec << std::endl;
                        if (parsed == 6 && year >= 1601 && month >= 1 && month <= 12 && day >= 1 && day <= 31) {
                            // Cross-platform: convert to Windows FILETIME ticks
                            std::tm tm = {};
                            tm.tm_year = year - 1900;
                            tm.tm_mon = month - 1;
                            tm.tm_mday = day;
                            tm.tm_hour = hour;
                            tm.tm_min = min;
                            tm.tm_sec = sec;
                            time_t unixTime = std::mktime(&tm);
                            // Convert Unix time to Windows FILETIME (100-ns intervals since 1601)
                            const int64_t FILETIME_UNIX_DIFF = 116444736000000000LL;
                            int64_t fileTimeTicks = (int64_t)unixTime * 10000000LL + FILETIME_UNIX_DIFF;
                            factoryReplayInfo.timestamp = fileTimeTicks + 504911232000000000LL;
                        }
                    }
                    if (editingJudgements) {
                        // Parse judgements string
                        int g300, n300, n200, n100, n50, miss;
                        if (sscanf(factoryJudgementsStr.c_str(), "%d,%d,%d,%d,%d,%d",
                                   &g300, &n300, &n200, &n100, &n50, &miss) == 6) {
                            factoryReplayInfo.count300g = g300;
                            factoryReplayInfo.count300 = n300;
                            factoryReplayInfo.count200 = n200;
                            factoryReplayInfo.count100 = n100;
                            factoryReplayInfo.count50 = n50;
                            factoryReplayInfo.countMiss = miss;
                        }
                    }
                    if (editingScore) {
                        try {
                            factoryReplayInfo.totalScore = std::stoi(factoryScoreStr);
                        } catch (...) {}
                    }
                    if (editingCombo) {
                        try {
                            factoryReplayInfo.maxCombo = std::stoi(factoryComboStr);
                        } catch (...) {}
                    }
                    // Clear all editing states
                    editingPlayerName = false;
                    editingTimestamp = false;
                    editingJudgements = false;
                    editingScore = false;
                    editingCombo = false;
                }
            }
            else if (state == GameState::KeyBinding) {
                if (e.key.key == SDLK_ESCAPE) {
                    state = GameState::Settings;
                } else if (!e.key.repeat) {
                    settings.keys[settings.selectedKeyCount - 1][keyBindingIndex] = e.key.key;
                    keyBindingIndex++;
                    if (keyBindingIndex >= settings.selectedKeyCount) {
                        keyBindingIndex = 0;
                        state = GameState::Settings;
                    }
                }
            }
            else if (state == GameState::Result) {
                if (e.key.key == SDLK_ESCAPE) {
                    audio.stop();
                    state = GameState::Menu;
                    renderer.setWindowTitle("Mania Player");
                }
            }
            else if (state == GameState::Paused && !e.key.repeat) {
                switch (e.key.key) {
                    case SDLK_UP:
                    case SDLK_LEFT:
                        pauseMenuSelection = (pauseMenuSelection + 2) % 3;
                        break;
                    case SDLK_DOWN:
                    case SDLK_RIGHT:
                        pauseMenuSelection = (pauseMenuSelection + 1) % 3;
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        if (pauseMenuSelection == 0) {
                            // Resume with fade out (audio resumes after fade completes)
                            // For keysound-only maps, adjust startTime immediately to prevent time jump
                            if (!hasBackgroundMusic) {
                                int64_t pausedDuration = SDL_GetTicks() - pauseTime;
                                startTime += pausedDuration;
                            }
                            pauseFadingOut = true;
                            pauseFadeOutStart = SDL_GetTicks();
                            state = GameState::Playing;
                        } else if (pauseMenuSelection == 1) {
                            // Retry
                            audio.stop();
                            startAsyncLoad(beatmapPath);
                        } else {
                            // Exit to song select
                            audio.stop();
                            audio.stopAllSamples();
                            state = GameState::SongSelect;
                            renderer.setWindowTitle("Mania Player");
                            if (!songList.empty()) {
                                loadSongBackground(selectedSongIndex, selectedDifficultyIndex);
                                playPreviewMusic(selectedSongIndex, selectedDifficultyIndex);
                            }
                        }
                        break;
                    case SDLK_ESCAPE: {
                        // ESC also resumes with fade out
                        // For keysound-only maps, adjust startTime immediately to prevent time jump
                        if (!hasBackgroundMusic) {
                            int64_t pausedDuration = SDL_GetTicks() - pauseTime;
                            startTime += pausedDuration;
                        }
                        pauseFadingOut = true;
                        pauseFadeOutStart = SDL_GetTicks();
                        state = GameState::Playing;
                        break;
                    }
                }
            }
            else if (state == GameState::Dead && !e.key.repeat) {
                switch (e.key.key) {
                    case SDLK_UP:
                    case SDLK_LEFT:
                        deathMenuSelection = (deathMenuSelection + 2) % 3;
                        break;
                    case SDLK_DOWN:
                    case SDLK_RIGHT:
                        deathMenuSelection = (deathMenuSelection + 1) % 3;
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        if (deathMenuSelection == 0) {
                            // Export Replay
                            std::string savePath = saveReplayDialog();
                            if (!savePath.empty()) {
                                // AutoPlay forces player name to "Mr.AutoPlay"
                                std::string exportPlayerName = autoPlay ? "Mr.AutoPlay" : settings.username;
                                int mods = 0;
                                if (autoPlay) mods |= 2048;              // Auto
                                if (settings.suddenDeathEnabled) mods |= 32;   // SD
                                if (settings.halfTimeEnabled) mods |= 256;     // HT
                                if (settings.doubleTimeEnabled) mods |= 64;    // DT
                                if (settings.nightcoreEnabled) mods |= 512;    // NC
                                if (settings.hiddenEnabled) mods |= 8;         // HD
                                if (settings.fadeInEnabled) mods |= 0x400000;  // FI
                                ReplayWriter::write(savePath, beatmap.beatmapHash, exportPlayerName, beatmap.keyCount,
                                                   judgementCounts, maxCombo, score, mods, recordedFrames);
                            }
                        } else if (deathMenuSelection == 1) {
                            // Retry
                            audio.stop();
                            startAsyncLoad(beatmapPath);
                        } else {
                            // Quit to song select
                            audio.stop();
                            audio.stopAllSamples();
                            state = GameState::SongSelect;
                            renderer.setWindowTitle("Mania Player");
                            if (!songList.empty()) {
                                loadSongBackground(selectedSongIndex, selectedDifficultyIndex);
                                playPreviewMusic(selectedSongIndex, selectedDifficultyIndex);
                            }
                        }
                        break;
                }
            }
            else if (state == GameState::Playing && !e.key.repeat) {
                switch (e.key.key) {
                    case SDLK_ESCAPE:
                        // If already fading out from previous resume, keep the frozen pauseGameTime
                        if (!pauseFadingOut) {
                            pauseGameTime = getCurrentGameTime();
                        }
                        pauseFadingOut = false;  // Cancel any ongoing fade out
                        audio.pause();
                        audio.pauseAllSamples();
                        pauseTime = SDL_GetTicks();
                        pauseMenuSelection = 0;
                        // Reset key states to prevent stuck pressed state
                        for (int i = 0; i < 10; i++) {
                            laneKeyDown[i] = false;
                        }
                        renderer.resetKeyReleaseTime();
                        state = GameState::Paused;
                        break;
                    case SDLK_SPACE:
                        // Skip functionality (disabled during pause fade out)
                        if (canSkip && !pauseFadingOut) {
                            int64_t currentTime = getCurrentGameTime();
                            if (currentTime < skipTargetTime) {
                                // Start music first if not started
                                if (!musicStarted) {
                                    if (hasBackgroundMusic) {
                                        audio.setVolume(settings.volume);  // Restore volume before play
                                        audio.play();
                                    }
                                    musicStarted = true;
                                }
                                // Then jump to skip target time
                                if (hasBackgroundMusic) {
                                    audio.setPosition(skipTargetTime);
                                }
                                // Adjust startTime so elapsed time calculation is correct
                                startTime = SDL_GetTicks() - (skipTargetTime + PREPARE_TIME);
                                canSkip = false;  // Disable skip after use
                            }
                        }
                        // Fall through to check if space is a game key
                        [[fallthrough]];
                    default:
                        // Don't allow note hits during pause fade out
                        if (!replayMode && !autoPlay && !pauseFadingOut) {
                            for (int i = 0; i < beatmap.keyCount; i++) {
                                if (e.key.key == settings.keys[beatmap.keyCount - 1][i]) {
                                    SDL_Log("KEY_INPUT: lane=%d key=%d time=%lld", i, (int)e.key.key, (long long)getCurrentGameTime());
                                    laneKeyDown[i] = true;
                                    checkJudgement(i);
                                    // Record frame
                                    int64_t currentTime = getCurrentGameTime();
                                    int keyState = 0;
                                    for (int k = 0; k < beatmap.keyCount; k++) {
                                        if (laneKeyDown[k]) keyState |= (1 << k);
                                    }
                                    if (keyState != lastRecordedKeyState) {
                                        recordedFrames.push_back({currentTime, keyState});
                                        lastRecordedKeyState = keyState;
                                    }
                                    break;
                                }
                            }
                        }
                        break;
                    case SDLK_EQUALS:
                        if (settings.unlimitedSpeed) {
                            settings.scrollSpeed = settings.scrollSpeed + 1;
                        } else {
                            settings.scrollSpeed = std::min(40, settings.scrollSpeed + 1);
                        }
                        saveConfig();
                        break;
                    case SDLK_MINUS:
                        settings.scrollSpeed = std::max(1, settings.scrollSpeed - 1);
                        saveConfig();
                        break;
                    case SDLK_TAB:
                        // Only allow Tab to toggle autoPlay in replay mode
                        if (replayMode) {
                            autoPlay = !autoPlay;
                        }
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        if (showEndPrompt) {
                            audio.stop();
                            cleanupTempDir();
                            state = GameState::Result;
                        }
                        break;
                }
            }
        }
        else if (e.type == SDL_EVENT_KEY_UP && state == GameState::Playing) {
            if (!replayMode && !autoPlay) {
                for (int i = 0; i < beatmap.keyCount; i++) {
                    if (e.key.key == settings.keys[beatmap.keyCount - 1][i]) {
                        laneKeyDown[i] = false;
                        onKeyRelease(i);
                        // Record frame
                        int64_t currentTime = getCurrentGameTime();
                        int keyState = 0;
                        for (int k = 0; k < beatmap.keyCount; k++) {
                            if (laneKeyDown[k]) keyState |= (1 << k);
                        }
                        if (keyState != lastRecordedKeyState) {
                            recordedFrames.push_back({currentTime, keyState});
                            lastRecordedKeyState = keyState;
                        }
                        break;
                    }
                }
            }
        }
    }
}

void Game::update() {
    int64_t elapsed = SDL_GetTicks() - startTime;

    // Don't start music during pause fade out
    if (!musicStarted && elapsed >= PREPARE_TIME && !pauseFadingOut) {
        if (hasBackgroundMusic) {
            audio.setVolume(settings.volume);  // Restore volume after preview
            audio.play();
            std::cout << "[DEBUG] Music started, volume=" << settings.volume << std::endl;
        } else {
            std::cout << "[DEBUG] No background music, skipping play" << std::endl;
        }
        musicStarted = true;
    }

    // For keysound-only maps, use system time instead of audio position
    int64_t currentTime;
    // During pause fade out, freeze game time to prevent miss judgements
    if (pauseFadingOut) {
        currentTime = pauseGameTime;
    } else if (!musicStarted) {
        // Prepare phase: scale time so note fall speed matches gameplay
        currentTime = static_cast<int64_t>((elapsed - PREPARE_TIME) * clockRate);
    } else if (hasBackgroundMusic) {
        currentTime = audio.getPosition();
    } else {
        // No background music: use elapsed time scaled by playback rate
        currentTime = static_cast<int64_t>((elapsed - PREPARE_TIME) * clockRate);
    }

    // Update skip availability
    if (canSkip && currentTime >= skipTargetTime) {
        canSkip = false;
    }

    // Update timing point volume for key sounds
    int tpVolume = 100;
    for (const auto& tp : beatmap.timingPoints) {
        if (tp.time <= currentTime) {
            // TimingPoint format: time,beatLength,meter,sampleSet,sampleIndex,volume,...
            // We need to parse volume from the original data, but for now use default
            tpVolume = 100;  // TODO: parse from timing point
        }
    }
    keySoundManager.setTimingPointVolume(tpVolume);

    // Play storyboard samples (limit per frame to prevent burst at end)
    int samplesPlayedThisFrame = 0;
    const int maxSamplesPerFrame = 20;  // Reasonable limit
    while (currentStoryboardSample < beatmap.storyboardSamples.size() &&
           samplesPlayedThisFrame < maxSamplesPerFrame) {
        auto& sample = beatmap.storyboardSamples[currentStoryboardSample];
        if (sample.time <= currentTime) {
            keySoundManager.playStoryboardSample(sample);
            currentStoryboardSample++;
            samplesPlayedThisFrame++;
        } else {
            break;
        }
    }

    // Update HP smoothing (deltaTime in seconds, assuming ~60fps = ~16.67ms per frame)
    double deltaTime = 16.67 / 1000.0;  // Approximate frame time
    hpManager.update(deltaTime);

    // Death mod: check if HP reached 0 (use targetHP, not smoothed currentHP)
    // Replay mode ignores Death mod
    if (settings.deathEnabled && !replayMode && hpManager.getTargetHP() <= 0.0 && !autoPlay) {
        state = GameState::Dead;
        deathTime = currentTime;
        deathSlowdown = 1.0f;
        deathMenuSelection = 1;  // Default to Retry
        audio.pause();
        return;
    }

    if (autoPlay) {
        bool keyStateChanged = false;
        for (auto& note : beatmap.notes) {
            if (note.state == NoteState::Waiting && note.time <= currentTime) {
                // Play key sound for note head
                keySoundManager.playKeySound(note, false);

                // Notify storyboard of hitsound and hit event
                storyboard.onHitSound(buildHitSoundInfo(note, beatmap.timingPoints, currentTime, false), currentTime);
                storyboard.onHitObjectHit(currentTime);

                if (note.isHold) {
                    // Hold note: start holding, set up ticks, no judgement yet
                    note.state = NoteState::Holding;
                    note.headHit = true;  // Mark head as hit so it stops at judge line
                    note.headHitEarly = false;  // AutoPlay hits exactly on time
                    note.headHitError = 0;
                    note.nextTickTime = note.time;
                    laneKeyDown[note.lane] = true;
                    keyStateChanged = true;
                } else {
                    // Regular note: immediate judgement
                    note.state = NoteState::Hit;
                    renderer.triggerLightingN(note.lane, currentTime);
                    processJudgement(judgementSystem.adjustForEnabled(Judgement::Marvelous), note.lane);
                    hitErrors.push_back({(int64_t)SDL_GetTicks(), 0});  // AutoPlay has 0 offset
                    // Record key press and release for regular note
                    int keyState = 0;
                    for (int k = 0; k < beatmap.keyCount; k++) {
                        if (laneKeyDown[k]) keyState |= (1 << k);
                    }
                    keyState |= (1 << note.lane);  // Press
                    if (keyState != lastRecordedKeyState) {
                        recordedFrames.push_back({currentTime, keyState});
                        lastRecordedKeyState = keyState;
                    }
                    keyState &= ~(1 << note.lane);  // Release
                    if (keyState != lastRecordedKeyState) {
                        recordedFrames.push_back({currentTime + 1, keyState});
                        lastRecordedKeyState = keyState;
                    }
                }
            }
            if (note.state == NoteState::Holding && note.isHold && note.endTime <= currentTime) {
                // Process remaining ticks before ending hold note
                while (note.nextTickTime + 100 <= note.endTime) {
                    note.nextTickTime += 100;
                    combo++;
                    if (combo > maxCombo) maxCombo = combo;
                    addDebugLog(currentTime, "AUTOPLAY_TICK", note.lane,
                        "combo=" + std::to_string(combo) + " maxCombo=" + std::to_string(maxCombo));
                }
                // Play tail key sound
                keySoundManager.playKeySound(note, true);

                // Notify storyboard of tail hitsound
                storyboard.onHitSound(buildHitSoundInfo(note, beatmap.timingPoints, currentTime, true), currentTime);

                // Hold note end: give judgement with combo and score
                note.state = NoteState::Hit;
                renderer.triggerLightingN(note.lane, currentTime);
                processJudgement(judgementSystem.adjustForEnabled(Judgement::Marvelous), note.lane);
                hitErrors.push_back({(int64_t)SDL_GetTicks(), 0});  // AutoPlay has 0 offset
                laneKeyDown[note.lane] = false;
                keyStateChanged = true;
            }
        }
        // Record key state change for hold notes
        if (keyStateChanged) {
            int keyState = 0;
            for (int k = 0; k < beatmap.keyCount; k++) {
                if (laneKeyDown[k]) keyState |= (1 << k);
            }
            if (keyState != lastRecordedKeyState) {
                recordedFrames.push_back({currentTime, keyState});
                lastRecordedKeyState = keyState;
            }
        }
    }

    // Replay mode: apply replay frames
    if (replayMode && !autoPlay) {
        // Mask to only use valid key bits for this key count
        int keyMask = (1 << beatmap.keyCount) - 1;
        // Check if Mirror mod is enabled (0x40000000)
        bool mirrorMod = (replayInfo.mods & 0x40000000) != 0;

        int framesProcessed = 0;
        while (currentReplayFrame < replayInfo.frames.size() &&
               replayInfo.frames[currentReplayFrame].time <= currentTime) {
            int64_t frameTime = replayInfo.frames[currentReplayFrame].time;
            int keyState = replayInfo.frames[currentReplayFrame].keyState & keyMask;

            // Mirror key state if Mirror mod is enabled
            if (mirrorMod) {
                int mirrored = 0;
                for (int i = 0; i < beatmap.keyCount; i++) {
                    if (keyState & (1 << i)) {
                        mirrored |= (1 << (beatmap.keyCount - 1 - i));
                    }
                }
                keyState = mirrored;
            }

            // Debug: print first few key presses
            static int debugKeyPresses = 0;
            if (keyState != 0 && debugKeyPresses < 10) {
                std::cout << "Replay frame: time=" << frameTime << " keys=" << keyState << " currentTime=" << currentTime << std::endl;
                debugKeyPresses++;
            }
            for (int i = 0; i < beatmap.keyCount; i++) {
                bool pressed = (keyState >> i) & 1;
                if (pressed && !laneKeyDown[i]) {
                    laneKeyDown[i] = true;
                    checkJudgement(i, frameTime);
                } else if (!pressed && laneKeyDown[i]) {
                    laneKeyDown[i] = false;
                    onKeyRelease(i, frameTime);
                }
            }
            // Record frame for replay mode
            if (keyState != lastRecordedKeyState) {
                recordedFrames.push_back({frameTime, keyState});
                lastRecordedKeyState = keyState;
            }
            currentReplayFrame++;
        }
    }

    for (auto& note : beatmap.notes) {
        if (note.isFakeNote) continue;  // Skip fake notes - visual only
        if (note.state == NoteState::Waiting && note.time < currentTime - judgementSystem.getBadWindow()) {
            if (note.isHold) {
                // Hold note head missed
                note.state = NoteState::Holding;
                note.headHitError = judgementSystem.getBadWindow();
                // If user is already holding the key, don't gray out, don't allow ticks
                // User must release and re-press to get combo
                if (laneKeyDown[note.lane]) {
                    // User was holding before note arrived - no gray, no combo
                    note.hadComboBreak = false;
                    note.nextTickTime = INT64_MAX;  // Disable ticks
                } else {
                    // Normal head miss - start gray transition
                    note.hadComboBreak = true;
                    note.nextTickTime = currentTime;  // Allow ticks when user presses back
                    note.headGrayStartTime = currentTime;  // Gray out immediately
                }
                combo = 0;  // Break combo when head is missed
            } else {
                note.state = NoteState::Missed;
                processJudgement(Judgement::Miss, note.lane);
            }
        }
        if (note.state == NoteState::Holding && note.isHold) {
            // Process hold note ticks (every 100ms)
            // ScoreV1: ticks only affect combo, not score
            if (laneKeyDown[note.lane] && currentTime <= note.endTime && currentTime >= note.time && note.nextTickTime != INT64_MAX) {
                while (note.nextTickTime + 100 <= currentTime) {
                    note.nextTickTime += 100;
                    combo++;
                    if (combo > maxCombo) maxCombo = combo;
                    addDebugLog(currentTime, "TICK", note.lane,
                        "combo=" + std::to_string(combo) + " maxCombo=" + std::to_string(maxCombo));
                }
            }
            // Check if hold note tail timed out
            if (note.endTime < currentTime - judgementSystem.getBadWindow()) {
                note.state = NoteState::Missed;
                // Record miss when tail times out (whole hold note counts as 1 miss)
                processJudgement(Judgement::Miss, note.lane);
            }
        }
        // Released hold notes - no ticks, but check for timeout
        if (note.state == NoteState::Released && note.isHold) {
            if (note.endTime < currentTime - judgementSystem.getBadWindow()) {
                note.state = NoteState::Missed;
                processJudgement(Judgement::Miss, note.lane);
            }
        }
    }

    // End game when music stops (only for maps with background music)
    // Don't trigger during pause fade out (audio is paused but not finished)
    if (hasBackgroundMusic && musicStarted && !audio.isPlaying() && !pauseFadingOut) {
        cleanupTempDir();
        state = GameState::Result;
    }

    // Check if all notes are finished
    if (musicStarted && !showEndPrompt && allNotesFinishedTime == 0) {
        bool allFinished = true;
        for (const auto& note : beatmap.notes) {
            if (note.state == NoteState::Waiting ||
                note.state == NoteState::Holding ||
                note.state == NoteState::Released) {
                allFinished = false;
                break;
            }
        }
        if (allFinished) {
            allNotesFinishedTime = SDL_GetTicks();
        }
    }

    // Show prompt after 2 seconds if music still playing
    if (allNotesFinishedTime > 0 && !showEndPrompt) {
        if (SDL_GetTicks() - allNotesFinishedTime >= 2000) {
            showEndPrompt = true;
        }
    }

    if (settings.lowSpecMode && hitErrors.size() > 20) {
        hitErrors.erase(hitErrors.begin(), hitErrors.begin() + (hitErrors.size() - 20));
    } else if (hitErrors.size() > 500) {
        // Limit hitErrors size to prevent performance degradation
        hitErrors.erase(hitErrors.begin(), hitErrors.begin() + (hitErrors.size() - 500));
    }
}

void Game::updateReplay() {
    int64_t currentTime = getCurrentGameTime();

    // Apply replay frames
    while (currentReplayFrame < replayInfo.frames.size() &&
           replayInfo.frames[currentReplayFrame].time <= currentTime) {
        int keyState = replayInfo.frames[currentReplayFrame].keyState;
        for (int i = 0; i < beatmap.keyCount; i++) {
            bool pressed = (keyState >> i) & 1;
            if (pressed && !laneKeyDown[i]) {
                laneKeyDown[i] = true;
                checkJudgement(i);
            } else if (!pressed && laneKeyDown[i]) {
                laneKeyDown[i] = false;
                onKeyRelease(i);
            }
        }
        currentReplayFrame++;
    }
}

void Game::render() {
    // Update preview fade (works in all states)
    updatePreviewFade();
    // Update async background load
    updateBackgroundLoad();

    frameCount++;
    int64_t now = SDL_GetTicks();
    if (now - lastFpsTime >= 1000) {
        fps = frameCount;
        frameCount = 0;
        lastFpsTime = now;
    }

    renderer.clear();

    if (state == GameState::Menu) {
        renderer.renderMenu();
        float btnW = 200, btnH = 50;
        float btnX = (1280 - btnW) / 2;
        float btnY = 320;

        // Only allow Select Beatmap if there are songs
        if (!songList.empty()) {
            if (renderer.renderButton("Select Beatmap", btnX, btnY, btnW, btnH, mouseX, mouseY, mouseClicked)) {
                replayMode = false;
                scanSongsFolder();  // Incremental scan for new/removed songs
                selectedSongIndex = 0;
                selectedDifficultyIndex = 0;
                songSelectScroll = 0.0f;
                if (!songList.empty()) {
                    loadSongBackground(0, 0);
                    playPreviewMusic(0, 0);
                }
                state = GameState::SongSelect;
            }
        } else {
            // Render disabled button (no click handling)
            renderer.renderButton("Select Beatmap", btnX, btnY, btnW, btnH, -1, -1, false);
            // Show warning message
            renderer.renderText("No Available Beatmap in Songs Folder.", btnX - 120, btnY + btnH + 130);
        }
        if (renderer.renderButton("Select Replay", btnX, btnY + 60, btnW, btnH, mouseX, mouseY, mouseClicked)) {
            std::string replayPath = openReplayDialog();
            if (!replayPath.empty() && ReplayParser::parse(replayPath, replayInfo)) {
                // Try to find matching beatmap by hash in songs list
                std::string beatmapPath;
                for (const auto& song : songList) {
                    for (const auto& diff : song.difficulties) {
                        if (diff.hash == replayInfo.beatmapHash) {
                            beatmapPath = diff.path;
                            break;
                        }
                    }
                    if (!beatmapPath.empty()) break;
                }

                // If not found, ask user to select manually
                if (beatmapPath.empty()) {
                    beatmapPath = openFileDialog();
                }

                if (!beatmapPath.empty()) {
                    startAsyncLoad(beatmapPath, true);
                }
            }
        }
        if (renderer.renderButton("Replay Factory", btnX, btnY + 120, btnW, btnH, mouseX, mouseY, mouseClicked)) {
            // Initialize factory state
            factoryReplayPath.clear();
            factoryReplayInfo = ReplayInfo();  // Reset to default (mods = 0)
            factoryMirrorInput = false;  // Reset mirror checkbox
            state = GameState::ReplayFactory;
        }
        if (renderer.renderButton("Settings", 20, 20, 100, 35, mouseX, mouseY, mouseClicked)) {
            state = GameState::Settings;
        }
    }
    else if (state == GameState::SongSelect) {
        // Transition animation
        float transitionOffset = 0;
        float transitionAlpha = 1.0f;
        if (songSelectTransition) {
            float elapsed = (float)(SDL_GetTicks() - songSelectTransitionStart);
            float duration = 500.0f;  // 500ms transition
            float t = elapsed / duration;
            if (t >= 1.0f) {
                // Transition complete, switch to playing
                songSelectTransition = false;
                replayMode = false;  // Reset replay mode when starting from song select
                autoPlay = settings.autoPlayEnabled;
                state = GameState::Playing;
                musicStarted = false;
                startTime = SDL_GetTicks();
                // Flush keyboard events to prevent ghost key presses at game start
                SDL_FlushEvent(SDL_EVENT_KEY_DOWN);
                SDL_FlushEvent(SDL_EVENT_KEY_UP);
                std::string title = "Mania Player - " + beatmap.artist + " " + beatmap.title;
                renderer.setWindowTitle(title);
            } else {
                // Ease out: panel slides right
                transitionOffset = t * t * 1280;  // Accelerate to the right
                transitionAlpha = 1.0f - t;
            }
        }

        // Render background
        if (currentBgTexture) {
            SDL_FRect bgRect = {0, 0, 1280, 720};
            SDL_RenderTexture(renderer.getRenderer(), currentBgTexture, nullptr, &bgRect);
        }

        // Song list panel (right 3/4 of screen, 90% opaque / 10% transparent)
        int panelX = 1280 / 4 + (int)transitionOffset;  // Slide right during transition
        int panelW = 1280 - 1280 / 4;
        SDL_SetRenderDrawColor(renderer.getRenderer(), 0, 0, 0, 230);  // 90% opaque
        SDL_FRect panel = {(float)panelX, 0, (float)panelW, 720};
        SDL_RenderFillRect(renderer.getRenderer(), &panel);

        // Render song list with expandable difficulties
        int rowHeight = 60;
        int diffRowHeight = 45;  // Smaller height for difficulty rows
        int startY = 50;
        float currentY = (float)startY;

        // First pass: calculate total height and find selected item's Y position
        float selectedItemY = 0;
        for (int i = 0; i < (int)songList.size(); i++) {
            if (i == selectedSongIndex) {
                selectedItemY = currentY + selectedDifficultyIndex * diffRowHeight;
                if (selectedDifficultyIndex > 0) {
                    selectedItemY += rowHeight;  // Account for song row
                }
            }
            currentY += rowHeight;
            if (i == selectedSongIndex) {
                currentY += (int)songList[i].beatmapFiles.size() * diffRowHeight;
            }
        }
        float totalHeight = currentY;

        // Auto scroll to keep selection visible (only when selection changed via keyboard)
        if (songSelectNeedAutoScroll) {
            if (selectedItemY - songSelectScroll < 60) {
                songSelectScroll = selectedItemY - 60;
            }
            if (selectedItemY - songSelectScroll > 600) {
                songSelectScroll = selectedItemY - 600;
            }
            songSelectNeedAutoScroll = false;
        }
        // Clamp scroll range
        if (songSelectScroll < 0) songSelectScroll = 0;
        float maxScroll = totalHeight - 660;
        if (maxScroll < 0) maxScroll = 0;
        if (songSelectScroll > maxScroll) songSelectScroll = maxScroll;

        // Second pass: render
        currentY = (float)startY;
        for (int i = 0; i < (int)songList.size(); i++) {
            float y = currentY - songSelectScroll;

            const SongEntry& song = songList[i];
            bool isSelected = (i == selectedSongIndex);

            // Render song row if visible
            if (y >= -rowHeight && y <= 720) {
                // Mouse click on song row (only if not dragging)
                bool isDragging = std::abs(mouseY - songSelectDragStartY) > 5;
                if (mouseClicked && !songSelectTransition && !isDragging) {
                    if (mouseX >= panelX && mouseX < panelX + panelW &&
                        mouseY >= y && mouseY < y + rowHeight) {
                        if (isSelected && selectedDifficultyIndex == 0) {
                            // Double click on selected song - confirm first difficulty
                            std::string path = song.beatmapFiles[0];
                            // Set version and hash for replay export
                            if (!song.difficulties.empty()) {
                                beatmap.version = song.difficulties[0].version;
                                beatmap.beatmapHash = song.difficulties[0].hash;
                            }
                            stopPreviewMusic();
                            startAsyncLoad(path);
                        } else {
                            selectedSongIndex = i;
                            selectedDifficultyIndex = 0;
                            loadSongBackground(i, 0);
                            playPreviewMusic(i, 0);
                        }
                    }
                }

                // Highlight selected song (bright)
                if (isSelected) {
                    SDL_SetRenderDrawColor(renderer.getRenderer(), 100, 100, 255, 100);
                    SDL_FRect highlight = {(float)panelX, y, (float)panelW, (float)rowHeight};
                    SDL_RenderFillRect(renderer.getRenderer(), &highlight);
                }

                // Song title and artist
                std::string displayText = song.title + " - " + song.artist;
                renderer.renderText(displayText.c_str(), (float)panelX + 20, y + 10);

                // Source label on the right (right-aligned)
                const char* sourceLabel = "";
                if (song.source == BeatmapSource::Osu) sourceLabel = "osu!";
                else if (song.source == BeatmapSource::DJMaxRespect) sourceLabel = "DJMAX RESPECT";
                else if (song.source == BeatmapSource::DJMaxOnline) sourceLabel = "DJMAX Online";
                else if (song.source == BeatmapSource::O2Jam) sourceLabel = "O2Jam";
                else if (song.source == BeatmapSource::BMS) sourceLabel = "BMS";
                else if (song.source == BeatmapSource::Malody) sourceLabel = "Malody";
                else if (song.source == BeatmapSource::MuSynx) sourceLabel = "MUSYNX";
                else if (song.source == BeatmapSource::IIDX) sourceLabel = "IIDX";
                renderer.renderTextRight(sourceLabel, 1280 - 20, y + 10);
            }

            currentY += rowHeight;

            // Render difficulty rows for selected song
            if (isSelected) {
                for (int d = 0; d < (int)song.beatmapFiles.size(); d++) {
                    float diffY = currentY - songSelectScroll;

                    if (diffY >= -diffRowHeight && diffY <= 720) {
                        // Mouse click on difficulty row (only if not dragging)
                        bool isDragging = std::abs(mouseY - songSelectDragStartY) > 5;
                        if (mouseClicked && !songSelectTransition && !isDragging) {
                            if (mouseX >= panelX && mouseX < panelX + panelW &&
                                mouseY >= diffY && mouseY < diffY + diffRowHeight) {
                                if (selectedDifficultyIndex == d) {
                                    // Double click - confirm
                                    std::string path = song.beatmapFiles[d];
                                    // Set version and hash for replay export
                                    if (d < (int)song.difficulties.size()) {
                                        beatmap.version = song.difficulties[d].version;
                                        beatmap.beatmapHash = song.difficulties[d].hash;
                                    }
                                    stopPreviewMusic();
                                    startAsyncLoad(path);
                                } else {
                                    selectedDifficultyIndex = d;
                                    loadSongBackground(selectedSongIndex, d);
                                    playPreviewMusic(selectedSongIndex, d);
                                }
                            }
                        }

                        // Highlight difficulty row (semi-bright, dimmer than song)
                        SDL_SetRenderDrawColor(renderer.getRenderer(), 60, 60, 180, 80);
                        SDL_FRect diffHighlight = {(float)panelX, diffY, (float)panelW, (float)diffRowHeight};
                        SDL_RenderFillRect(renderer.getRenderer(), &diffHighlight);

                        // Extra highlight for selected difficulty
                        if (d == selectedDifficultyIndex) {
                            SDL_SetRenderDrawColor(renderer.getRenderer(), 100, 100, 255, 60);
                            SDL_RenderFillRect(renderer.getRenderer(), &diffHighlight);
                        }

                        // Use DifficultyInfo if available
                        std::string diffName = formatDifficultyName(song, d, settings.starRatingVersion);

                        renderer.renderText(diffName.c_str(), (float)panelX + 50, diffY + 8);
                    }

                    currentY += diffRowHeight;
                }
            }
        }

        // Back button
        if (renderer.renderButton("Back", 20, 20, 80, 35, mouseX, mouseY, mouseClicked)) {
            stopPreviewMusic();
            if (currentBgTexture) {
                SDL_DestroyTexture(currentBgTexture);
                currentBgTexture = nullptr;
            }
            state = GameState::Menu;
        }
    }
    else if (state == GameState::Result) {
        renderer.renderResult(beatmap.title, beatmap.creator, judgementCounts,
                              calculateAccuracy(), maxCombo, score);
        float btnW = 150, btnH = 40;
        float btnX = 1280 - btnW - 20;
        float btnY = 720 - btnH - 20;

        // Back button (bottom)
        if (renderer.renderButton("Back", btnX, btnY, btnW, btnH, mouseX, mouseY, mouseClicked)) {
            state = GameState::Menu;
            renderer.setWindowTitle("Mania Player");
        }
        btnY -= btnH + 10;

        // Export Replay button
        if (renderer.renderButton("Export Replay", btnX, btnY, btnW, btnH, mouseX, mouseY, mouseClicked)) {
            std::string savePath = saveReplayDialog();
            if (!savePath.empty()) {
                // Determine player name for export
                // AutoPlay forces player name to "Mr.AutoPlay"
                std::string exportPlayerName;
                if (autoPlay) {
                    exportPlayerName = "Mr.AutoPlay";
                } else if (replayMode && !settings.forceOverrideUsername) {
                    exportPlayerName = replayInfo.playerName;
                } else {
                    exportPlayerName = settings.username;
                }
                // Determine mods
                int mods = 0;
                if (autoPlay) mods |= 2048;              // Auto
                if (settings.suddenDeathEnabled) mods |= 32;   // SD
                if (settings.halfTimeEnabled) mods |= 256;     // HT
                if (settings.doubleTimeEnabled) mods |= 64;    // DT
                if (settings.nightcoreEnabled) mods |= 512;    // NC
                if (settings.hiddenEnabled) mods |= 8;         // HD
                if (settings.fadeInEnabled) mods |= 0x400000;  // FI
                ReplayWriter::write(savePath, beatmap.beatmapHash, exportPlayerName, beatmap.keyCount,
                                   judgementCounts, maxCombo, score, mods, recordedFrames);
            }
        }

        // Export Log button (only when debug enabled)
        if (settings.debugEnabled) {
            btnY -= btnH + 10;
            if (renderer.renderButton("Export Log", btnX, btnY, btnW, btnH, mouseX, mouseY, mouseClicked)) {
                exportDebugLog();
            }
        }
    }
    else if (state == GameState::Loading) {
        // Dark background
        SDL_SetRenderDrawColor(renderer.getRenderer(), 20, 20, 20, 255);
        SDL_FRect bgRect = {0, 0, 1280, 720};
        SDL_RenderFillRect(renderer.getRenderer(), &bgRect);

        // Progress bar (centered, same style as video generation)
        float barW = 600;
        float barH = 30;
        float barX = (1280 - barW) / 2;
        float barY = (720 - barH) / 2;

        // Background
        SDL_SetRenderDrawColor(renderer.getRenderer(), 40, 40, 40, 255);
        SDL_FRect barBg = {barX, barY, barW, barH};
        SDL_RenderFillRect(renderer.getRenderer(), &barBg);

        // Progress fill
        float progress = loadingProgress.load();
        SDL_SetRenderDrawColor(renderer.getRenderer(), 100, 150, 255, 255);
        SDL_FRect fillRect = {barX, barY, barW * progress, barH};
        SDL_RenderFillRect(renderer.getRenderer(), &fillRect);

        // Border
        SDL_SetRenderDrawColor(renderer.getRenderer(), 100, 100, 100, 255);
        SDL_RenderRect(renderer.getRenderer(), &barBg);

        // Status text
        std::string statusText;
        {
            std::lock_guard<std::mutex> lock(loadingMutex);
            statusText = loadingStatusText;
        }
        char percentText[32];
        snprintf(percentText, sizeof(percentText), " (%.0f%%)", progress * 100);
        statusText += percentText;
        renderer.renderText(statusText.c_str(), barX, barY - 30);

        // ESC hint
        renderer.renderText("Press ESC to cancel", barX, barY + barH + 10);
    }
    else if (state == GameState::ReplayFactory) {
        // Title
        renderer.renderLabel("Replay Factory", 1280/2 - 100, 50);

        // Import | Filename | Export row (centered)
        float rowY = 100;
        float btnW = 80, btnH = 35;
        float fileBoxW = 300;
        float totalW = btnW + 10 + fileBoxW + 10 + btnW;  // Import + gap + box + gap + Export
        float startX = (1280 - totalW) / 2;

        // Import button
        if (renderer.renderButton("Import", startX, rowY, btnW, btnH, mouseX, mouseY, mouseClicked)) {
            std::string path = openReplayDialog();
            if (!path.empty()) {
                if (ReplayParser::parse(path, factoryReplayInfo)) {
                    factoryReplayPath = path;
                    factoryMirrorInput = false;  // Reset mirror checkbox on new import

                    // Create mirrored copy using auto-detected key count
                    factoryReplayInfoMirrored = factoryReplayInfo;
                    int keyCount = ReplayParser::detectKeyCount(factoryReplayInfo);
                    ReplayParser::mirrorKeys(factoryReplayInfoMirrored, keyCount);

                    // Debug: print first few frames after import
                    std::cout << "[Import] Loaded " << factoryReplayInfo.frames.size() << " frames" << std::endl;
                    for (size_t i = 0; i < 5 && i < factoryReplayInfo.frames.size(); i++) {
                        std::cout << "[Import] Frame " << i << ": x=" << factoryReplayInfo.frames[i].x
                                  << ", y=" << factoryReplayInfo.frames[i].y << std::endl;
                    }
                }
            }
        }

        // Filename box
        float boxX = startX + btnW + 10;
        SDL_SetRenderDrawColor(renderer.getRenderer(), 40, 40, 40, 255);
        SDL_FRect boxRect = {boxX, rowY, fileBoxW, btnH};
        SDL_RenderFillRect(renderer.getRenderer(), &boxRect);
        SDL_SetRenderDrawColor(renderer.getRenderer(), 100, 100, 100, 255);
        SDL_RenderRect(renderer.getRenderer(), &boxRect);

        // Render filename (clipped to box)
        if (!factoryReplayPath.empty()) {
            std::string filename = factoryReplayPath;
            size_t pos = filename.find_last_of("\\/");
            if (pos != std::string::npos) filename = filename.substr(pos + 1);
            renderer.renderTextClipped(filename.c_str(), boxX + 5, rowY + 8, fileBoxW - 10);
        }

        // Export button
        float exportX = boxX + fileBoxW + 10;
        if (renderer.renderButton("Export", exportX, rowY, btnW, btnH, mouseX, mouseY, mouseClicked)) {
            if (!factoryReplayPath.empty()) {
                ReplayInfo exportInfo;
                if (factoryMirrorInput) {
                    // Sync modifications to mirrored version before export
                    exportInfo = factoryReplayInfoMirrored;
                    exportInfo.playerName = factoryReplayInfo.playerName;
                    exportInfo.mods = factoryReplayInfo.mods;
                    exportInfo.timestamp = factoryReplayInfo.timestamp;
                    exportInfo.count300g = factoryReplayInfo.count300g;
                    exportInfo.count300 = factoryReplayInfo.count300;
                    exportInfo.count200 = factoryReplayInfo.count200;
                    exportInfo.count100 = factoryReplayInfo.count100;
                    exportInfo.count50 = factoryReplayInfo.count50;
                    exportInfo.countMiss = factoryReplayInfo.countMiss;
                    exportInfo.totalScore = factoryReplayInfo.totalScore;
                    exportInfo.maxCombo = factoryReplayInfo.maxCombo;
                    exportInfo.beatmapHash = factoryReplayInfo.beatmapHash;
                } else {
                    exportInfo = factoryReplayInfo;
                }

                // Add watermark before saving
                exportInfo.onlineScoreId = ReplayParser::createWatermark();

                // Save to _edited.osr file (don't modify original)
                std::string editedPath = factoryReplayPath;
                size_t dotPos = editedPath.rfind('.');
                if (dotPos != std::string::npos) {
                    editedPath.insert(dotPos, "_edited");
                } else {
                    editedPath += "_edited";
                }
                ReplayParser::save(editedPath, exportInfo);
            }
        }

        // Watermark status display
        if (!factoryReplayPath.empty() && ReplayParser::hasWatermark(factoryReplayInfo.onlineScoreId)) {
            int64_t wmTime = ReplayParser::getWatermarkTime(factoryReplayInfo.onlineScoreId);
            // Convert ms timestamp to readable format
            time_t seconds = wmTime / 1000;
            struct tm* tm_info = localtime(&seconds);
            char timeBuf[64];
            strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm_info);
            char wmText[128];
            snprintf(wmText, sizeof(wmText), "Last Modified: %s", timeBuf);
            renderer.renderText(wmText, exportX + btnW + 10, rowY + 10);
        }

        // Mods section (left 1/4 of screen)
        float modsX = 20;
        float modsY = 170;  // Align with Metadata and Analyze
        float modsW = 1280 / 4 - 40;
        renderer.renderLabel("Mods", modsX, modsY);

        // Mod checkboxes
        struct ModDef { const char* name; int flag; };
        ModDef mods[] = {
            {"NoFail", OsuMods::NoFail},
            {"Easy", OsuMods::Easy},
            {"TouchDevice", OsuMods::TouchDevice},
            {"Hidden", OsuMods::Hidden},
            {"HardRock", OsuMods::HardRock},
            {"SuddenDeath", OsuMods::SuddenDeath},
            {"DoubleTime", OsuMods::DoubleTime},
            {"Relax", OsuMods::Relax},
            {"HalfTime", OsuMods::HalfTime},
            {"Nightcore", OsuMods::Nightcore},
            {"Flashlight", OsuMods::Flashlight},
            {"Autoplay", OsuMods::Autoplay},
            {"SpunOut", OsuMods::SpunOut},
            {"Autopilot", OsuMods::Relax2},
            {"Perfect", OsuMods::Perfect},
            {"Key4", OsuMods::Key4},
            {"Key5", OsuMods::Key5},
            {"Key6", OsuMods::Key6},
            {"Key7", OsuMods::Key7},
            {"Key8", OsuMods::Key8},
            {"FadeIn", OsuMods::FadeIn},
            {"Random", OsuMods::Random},
            {"Cinema", OsuMods::Cinema},
            {"Target", OsuMods::Target},
            {"Key9", OsuMods::Key9},
            {"KeyCoop", OsuMods::KeyCoop},
            {"Key1", OsuMods::Key1},
            {"Key3", OsuMods::Key3},
            {"Key2", OsuMods::Key2},
            {"ScoreV2", OsuMods::ScoreV2},
            {"Mirror", OsuMods::Mirror},
        };

        float checkY = modsY + 30;
        int modCount = sizeof(mods) / sizeof(mods[0]);
        int halfCount = (modCount + 1) / 2;
        float col2X = modsX + 170;  // Second column X position

        for (int i = 0; i < modCount; i++) {
            const auto& mod = mods[i];
            float x = (i < halfCount) ? modsX : col2X;
            float y = checkY + (i % halfCount) * 22;

            bool checked = (factoryReplayInfo.mods & mod.flag) != 0;
            if (renderer.renderCheckbox(mod.name, checked, x, y, mouseX, mouseY, mouseClicked)) {
                if (checked) {
                    factoryReplayInfo.mods &= ~mod.flag;
                } else {
                    factoryReplayInfo.mods |= mod.flag;
                }
            }
        }

        // Metadata section (2nd quarter of screen)
        float metaX = 1280 / 4 + 20;
        float metaY = 170;  // Moved down 20px
        float metaW = 1280 / 4 - 40;  // Only 1/4 width
        renderer.renderLabel("Metadata", metaX, metaY);

        // Handle text input enable/disable
        bool anyEditing = editingPlayerName || editingTimestamp || editingJudgements || editingScore || editingCombo || editingBlockHeight || editingVideoWidth || editingVideoHeight || editingVideoFPS;
        static bool wasAnyEditing = false;
        if (anyEditing && !wasAnyEditing) {
            SDL_StartTextInput(renderer.getWindow());
        } else if (!anyEditing && wasAnyEditing) {
            SDL_StopTextInput(renderer.getWindow());
        }
        wasAnyEditing = anyEditing;

        float inputY = metaY + 30;
        float inputW = metaW - 10;
        float inputSpacing = 70;  // More spacing between fields

        // Player Name
        renderer.renderLabel("Player Name", metaX, inputY);
        renderer.renderTextInput(nullptr, factoryReplayInfo.playerName, metaX, inputY + 40, inputW, mouseX, mouseY, mouseClicked, editingPlayerName, cursorPos);
        inputY += inputSpacing;

        // Timestamp (convert to string for editing)
        if (!editingTimestamp && !factoryReplayPath.empty()) {
            // osu! timestamp is .NET DateTime.Ticks (from 0001-01-01)
            // Convert to FILETIME (from 1601-01-01) by subtracting 1600 years
            // Offset: 504911232000000000 ticks (1600 years)
            int64_t fileTimeTicks = factoryReplayInfo.timestamp - 504911232000000000LL;
            if (fileTimeTicks > 0) {
                // Cross-platform: convert FILETIME to local time
                const int64_t FILETIME_UNIX_DIFF = 116444736000000000LL;
                time_t unixTime = (fileTimeTicks - FILETIME_UNIX_DIFF) / 10000000LL;
                std::tm* tm = std::localtime(&unixTime);
                if (tm) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%04d/%02d/%02d %02d:%02d:%02d",
                             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                             tm->tm_hour, tm->tm_min, tm->tm_sec);
                    factoryTimestampStr = buf;
                } else {
                    factoryTimestampStr = "Invalid timestamp";
                }
            } else {
                factoryTimestampStr = "Invalid timestamp";
            }
        }
        renderer.renderLabel("Timestamp", metaX, inputY);
        renderer.renderText("(yyyy/mm/dd hh:mm:ss)", metaX, inputY + 18);
        renderer.renderTextInput(nullptr, factoryTimestampStr, metaX, inputY + 55, inputW, mouseX, mouseY, mouseClicked, editingTimestamp, cursorPos);
        inputY += inputSpacing + 15;

        // Judgements (300g,300,200,100,50,miss)
        if (!editingJudgements && !factoryReplayPath.empty()) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%d,%d",
                     factoryReplayInfo.count300g, factoryReplayInfo.count300,
                     factoryReplayInfo.count200, factoryReplayInfo.count100,
                     factoryReplayInfo.count50, factoryReplayInfo.countMiss);
            factoryJudgementsStr = buf;
        }
        renderer.renderLabel("Judgements", metaX, inputY);
        renderer.renderText("(300g,300,200,100,50,miss)", metaX, inputY + 18);
        renderer.renderTextInput(nullptr, factoryJudgementsStr, metaX, inputY + 55, inputW, mouseX, mouseY, mouseClicked, editingJudgements, cursorPos);
        inputY += inputSpacing + 15;

        // Score
        if (!editingScore && !factoryReplayPath.empty()) {
            factoryScoreStr = std::to_string(factoryReplayInfo.totalScore);
        }
        renderer.renderLabel("Score", metaX, inputY);
        renderer.renderTextInput(nullptr, factoryScoreStr, metaX, inputY + 40, inputW, mouseX, mouseY, mouseClicked, editingScore, cursorPos);
        inputY += inputSpacing;

        // Combo
        if (!editingCombo && !factoryReplayPath.empty()) {
            factoryComboStr = std::to_string(factoryReplayInfo.maxCombo);
        }
        renderer.renderLabel("Max Combo", metaX, inputY);
        renderer.renderTextInput(nullptr, factoryComboStr, metaX, inputY + 40, inputW, mouseX, mouseY, mouseClicked, editingCombo, cursorPos);
        inputY += inputSpacing;

        // Mirror Input checkbox
        if (renderer.renderCheckbox("Mirror Input", factoryMirrorInput, metaX, inputY + 20, mouseX, mouseY, mouseClicked)) {
            factoryMirrorInput = !factoryMirrorInput;
        }

        // Analyze section (3rd quarter of screen)
        float analyzeX = 1280 / 2 + 20;
        float analyzeY = 170;
        renderer.renderLabel("Analyze", analyzeX, analyzeY);

        float analyzeBtnY = analyzeY + 30;
        float analyzeBtnW = 180, analyzeBtnH = 35;

        // Block clicks when analysis window is open
        bool analysisBlockClick = showAnalysisWindow;

        // Press Distribution button
        if (!analysisBlockClick && renderer.renderButton("Press Distribution", analyzeX, analyzeBtnY, analyzeBtnW, analyzeBtnH, mouseX, mouseY, mouseClicked)) {
            if (!factoryReplayPath.empty()) {
                analysisResult = ReplayAnalyzer::analyze(factoryReplayInfo);
                analysisWindowType = 0;
                showAnalysisWindow = true;
            }
        }

        // Realtime Press button
        if (!analysisBlockClick && renderer.renderButton("Realtime Press", analyzeX, analyzeBtnY + 45, analyzeBtnW, analyzeBtnH, mouseX, mouseY, mouseClicked)) {
            if (!factoryReplayPath.empty()) {
                analysisResult = ReplayAnalyzer::analyze(factoryReplayInfo);
                analysisWindowType = 1;
                showAnalysisWindow = true;
            }
        }

        // Repair section
        float repairY = analyzeBtnY + 100;
        renderer.renderLabel("Repair", analyzeX, repairY);

        // Fix Beatmap Hash button
        if (!analysisBlockClick && !factoryFixHashPending && renderer.renderButton("Fix Beatmap Hash", analyzeX, repairY + 30, analyzeBtnW, analyzeBtnH, mouseX, mouseY, mouseClicked)) {
            if (!factoryReplayPath.empty()) {
                // Open file dialog to select correct beatmap
                std::string beatmapPath = openFileDialog();
                if (!beatmapPath.empty()) {
                    // Calculate new hash based on file type
                    std::string ext = beatmapPath.substr(beatmapPath.find_last_of('.'));
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                    if (ext == ".ojn") {
                        // O2Jam: need difficulty selection
                        factoryFixHashPending = true;
                        factoryFixHashPath = beatmapPath;
                        factoryFixHashType = 1;  // O2Jam
                    } else if (ext == ".1") {
                        // IIDX: need difficulty selection
                        factoryFixHashPending = true;
                        factoryFixHashPath = beatmapPath;
                        factoryFixHashType = 2;  // IIDX
                    } else {
                        // All other formats: just use file MD5
                        std::string newHash = OsuParser::calculateMD5(beatmapPath);
                        if (!newHash.empty()) {
                            factoryReplayInfo.beatmapHash = newHash;
                        }
                    }
                }
            }
        }

        // Difficulty selection popup for Fix Hash
        if (factoryFixHashPending) {
            int winW, winH;
            SDL_GetWindowSize(renderer.getWindow(), &winW, &winH);

            // Draw semi-transparent overlay
            SDL_SetRenderDrawBlendMode(renderer.getRenderer(), SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer.getRenderer(), 0, 0, 0, 180);
            SDL_FRect overlay = {0, 0, (float)winW, (float)winH};
            SDL_RenderFillRect(renderer.getRenderer(), &overlay);

            // Popup box size depends on type
            float popupW = (factoryFixHashType == 2) ? 340 : 300;
            float popupH = (factoryFixHashType == 2) ? 320 : 220;
            float popupX = (winW - popupW) / 2 + 30;
            float popupY = (winH - popupH) / 2;
            SDL_SetRenderDrawColor(renderer.getRenderer(), 40, 40, 40, 255);
            SDL_FRect popupBg = {popupX, popupY, popupW, popupH};
            SDL_RenderFillRect(renderer.getRenderer(), &popupBg);

            // Title
            renderer.renderLabel("Select Difficulty", popupX + 20, popupY + 10);

            if (factoryFixHashType == 1) {
                // O2Jam: Easy, Normal, Hard
                float btnW = 120, btnH = 30;
                float btnX = popupX + (popupW - btnW) / 2;
                float btnY = popupY + 50;
                if (renderer.renderButton("Easy", btnX, btnY, btnW, btnH, mouseX, mouseY, mouseClicked)) {
                    std::string newHash = OsuParser::calculateMD5(factoryFixHashPath) + ":0";
                    factoryReplayInfo.beatmapHash = newHash;
                    factoryFixHashPending = false;
                }
                if (renderer.renderButton("Normal", btnX, btnY + 40, btnW, btnH, mouseX, mouseY, mouseClicked)) {
                    std::string newHash = OsuParser::calculateMD5(factoryFixHashPath) + ":1";
                    factoryReplayInfo.beatmapHash = newHash;
                    factoryFixHashPending = false;
                }
                if (renderer.renderButton("Hard", btnX, btnY + 80, btnW, btnH, mouseX, mouseY, mouseClicked)) {
                    std::string newHash = OsuParser::calculateMD5(factoryFixHashPath) + ":2";
                    factoryReplayInfo.beatmapHash = newHash;
                    factoryFixHashPending = false;
                }
                // Cancel button
                if (renderer.renderButton("Cancel", btnX, popupY + popupH - 45, btnW, btnH, mouseX, mouseY, mouseClicked)) {
                    factoryFixHashPending = false;
                }
            } else if (factoryFixHashType == 2) {
                // IIDX: SP and DP columns
                float btnW = 130, btnH = 28;
                float colSpacing = 20;
                float spX = popupX + colSpacing;
                float dpX = popupX + popupW / 2 + colSpacing / 2;
                float btnY = popupY + 50;

                // SP column
                renderer.renderLabel("SP", spX + btnW / 2 - 10, btnY - 25);
                const char* spDiffs[] = {"NORMAL", "HYPER", "ANOTHER", "LEGGENDARIA"};
                int spIndices[] = {1, 2, 3, 4};
                for (int i = 0; i < 4; i++) {
                    if (renderer.renderButton(spDiffs[i], spX, btnY + i * 38, btnW, btnH, mouseX, mouseY, mouseClicked)) {
                        std::string newHash = OsuParser::calculateMD5(factoryFixHashPath) + ":" + std::to_string(spIndices[i]);
                        factoryReplayInfo.beatmapHash = newHash;
                        factoryFixHashPending = false;
                    }
                }

                // DP column
                renderer.renderLabel("DP", dpX + btnW / 2 - 10, btnY - 25);
                int dpIndices[] = {6, 7, 8, 9};
                for (int i = 0; i < 4; i++) {
                    if (renderer.renderButton(spDiffs[i], dpX, btnY + i * 38, btnW, btnH, mouseX, mouseY, mouseClicked)) {
                        std::string newHash = OsuParser::calculateMD5(factoryFixHashPath) + ":" + std::to_string(dpIndices[i]);
                        factoryReplayInfo.beatmapHash = newHash;
                        factoryFixHashPending = false;
                    }
                }

                // Cancel button
                float cancelX = popupX + (popupW - btnW) / 2;
                if (renderer.renderButton("Cancel", cancelX, popupY + popupH - 45, btnW, btnH, mouseX, mouseY, mouseClicked)) {
                    factoryFixHashPending = false;
                }
            }
        }

        // Visualization section
        float vizY = repairY + 80;
        renderer.renderLabel("Visualization", analyzeX, vizY);

        // Generate Video button
        bool videoRunning = videoGenerator.isRunning();
        if (!analysisBlockClick && !videoRunning && renderer.renderButton("Generate Video", analyzeX, vizY + 30, analyzeBtnW, analyzeBtnH, mouseX, mouseY, mouseClicked)) {
            if (!factoryReplayPath.empty()) {
                // Find beatmap by hash
                std::string beatmapPath;
                std::string audioPath;
                for (const auto& song : songList) {
                    for (const auto& diff : song.difficulties) {
                        if (diff.hash == factoryReplayInfo.beatmapHash) {
                            beatmapPath = diff.path;
                            audioPath = song.audioPath;
                            break;
                        }
                    }
                    if (!beatmapPath.empty()) break;
                }

                // If not found, ask user to select
                if (beatmapPath.empty()) {
                    beatmapPath = openFileDialog();
                }

                if (!beatmapPath.empty()) {
                    // Parse beatmap based on file type
                    BeatmapInfo videoBeatmap;
                    bool parseSuccess = false;
                    std::string ext = beatmapPath.substr(beatmapPath.find_last_of('.'));
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                    // Handle O2Jam path format (path:difficulty:level)
                    std::string actualPath = beatmapPath;
                    int ojnDifficulty = 2;  // Default Hard
                    if (beatmapPath.find(".ojn:") != std::string::npos) {
                        size_t colonPos = beatmapPath.find(".ojn:");
                        actualPath = beatmapPath.substr(0, colonPos + 4);
                        std::string suffix = beatmapPath.substr(colonPos + 5);
                        ojnDifficulty = std::stoi(suffix.substr(0, 1));
                        ext = ".ojn";
                    }

                    // Handle IIDX path format (path:diffIdx)
                    int iidxDiffIdx = 3;  // Default SP ANOTHER
                    if (ext == ".1" && beatmapPath.find(":") != std::string::npos) {
                        size_t colonPos = beatmapPath.find_last_of(':');
                        actualPath = beatmapPath.substr(0, colonPos);
                        iidxDiffIdx = std::stoi(beatmapPath.substr(colonPos + 1));
                    }

                    if (ext == ".osu") {
                        parseSuccess = OsuParser::parse(actualPath, videoBeatmap);
                    } else if (ext == ".ojn") {
                        parseSuccess = OjnParser::parse(actualPath, videoBeatmap, static_cast<OjnDifficulty>(ojnDifficulty));
                    } else if (ext == ".pt") {
                        parseSuccess = PTParser::parse(actualPath, videoBeatmap);
                    } else if (ext == ".bytes") {
                        parseSuccess = DJMaxParser::parse(actualPath, videoBeatmap);
                    } else if (ext == ".bms" || ext == ".bme" || ext == ".bml" || ext == ".pms") {
                        parseSuccess = BMSParser::parse(actualPath, videoBeatmap);
                    } else if (ext == ".mc") {
                        parseSuccess = MalodyParser::parse(actualPath, videoBeatmap);
                    } else if (ext == ".txt") {
                        parseSuccess = MuSynxParser::parse(actualPath, videoBeatmap);
                    } else if (ext == ".1") {
                        parseSuccess = IIDXParser::parse(actualPath, videoBeatmap, iidxDiffIdx);
                    }

                    if (parseSuccess) {
                        // Get audio path if not found
                        if (audioPath.empty() && !videoBeatmap.audioFilename.empty()) {
                            size_t lastSlash = beatmapPath.find_last_of("/\\");
                            if (lastSlash != std::string::npos) {
                                audioPath = beatmapPath.substr(0, lastSlash + 1) + videoBeatmap.audioFilename;
                            }
                        }

                        // Configure video
                        VideoConfig config;
                        config.audioPath = audioPath;
                        config.includeAudio = !audioPath.empty();

                        // Parse video settings from input
                        try { config.width = std::max(100, std::stoi(videoWidthInput)); } catch (...) { config.width = 540; }
                        try { config.height = std::max(100, std::stoi(videoHeightInput)); } catch (...) { config.height = 960; }
                        try { config.fps = std::max(1, std::min(120, std::stoi(videoFPSInput))); } catch (...) { config.fps = 60; }
                        try { config.blockHeight = std::max(10, std::stoi(blockHeightInput)); } catch (...) { config.blockHeight = 40; }
                        config.showHolding = videoShowHolding;

                        // Check replay mods for speed modifiers
                        if (factoryReplayInfo.mods & (OsuMods::DoubleTime | OsuMods::Nightcore)) {
                            config.clockRate = 1.5;
                            config.isNightcore = (factoryReplayInfo.mods & OsuMods::Nightcore) != 0;
                        } else if (factoryReplayInfo.mods & OsuMods::HalfTime) {
                            config.clockRate = 0.75;
                            config.isNightcore = false;
                        } else {
                            config.clockRate = 1.0;
                            config.isNightcore = false;
                        }

                        // Generate output path
                        size_t lastSlash = factoryReplayPath.find_last_of("/\\");
                        std::string replayName = (lastSlash != std::string::npos) ?
                            factoryReplayPath.substr(lastSlash + 1) : factoryReplayPath;
                        size_t dotPos = replayName.find_last_of('.');
                        if (dotPos != std::string::npos) {
                            replayName = replayName.substr(0, dotPos);
                        }
                        config.outputPath = "Exports/" + replayName + ".mp4";

                        // Create output directory
                        std::filesystem::create_directories("Exports");
                        std::filesystem::create_directories("Data/Tmp");

                        // Start generation
                        videoGenerator.startGeneration(factoryReplayInfo, videoBeatmap, settings, config, "Data/Tmp");
                    }
                }
            }
        }

        // Block Height input (below Generate Video button)
        float blockHeightY = vizY + 70;
        renderer.renderLabel("Block Height:", analyzeX, blockHeightY);
        renderer.renderTextInput(nullptr, blockHeightInput, analyzeX + 120, blockHeightY - 5, 50, mouseX, mouseY, mouseClicked, editingBlockHeight, cursorPos);

        // Width input
        float widthY = blockHeightY + 35;
        renderer.renderLabel("Width:", analyzeX, widthY);
        renderer.renderTextInput(nullptr, videoWidthInput, analyzeX + 120, widthY - 5, 50, mouseX, mouseY, mouseClicked, editingVideoWidth, cursorPos);

        // Height input
        float heightY = widthY + 35;
        renderer.renderLabel("Height:", analyzeX, heightY);
        renderer.renderTextInput(nullptr, videoHeightInput, analyzeX + 120, heightY - 5, 50, mouseX, mouseY, mouseClicked, editingVideoHeight, cursorPos);

        // FPS input
        float fpsY = heightY + 35;
        renderer.renderLabel("FPS:", analyzeX, fpsY);
        renderer.renderTextInput(nullptr, videoFPSInput, analyzeX + 120, fpsY - 5, 50, mouseX, mouseY, mouseClicked, editingVideoFPS, cursorPos);

        // Show Holding checkbox
        float showHoldingY = fpsY + 35;
        if (renderer.renderCheckbox("Show Holding", videoShowHolding, analyzeX, showHoldingY, mouseX, mouseY, mouseClicked)) {
            videoShowHolding = !videoShowHolding;
        }

        // Video generation progress bar
        if (videoGenerator.isRunning() || videoGenerator.getState() == VideoGenState::Completed ||
            videoGenerator.getState() == VideoGenState::Failed) {
            float barX = 100;
            float barY = 630;
            float barW = 1080;
            float barH = 25;

            // Background
            SDL_SetRenderDrawColor(renderer.getRenderer(), 40, 40, 40, 255);
            SDL_FRect bgRect = {barX, barY, barW, barH};
            SDL_RenderFillRect(renderer.getRenderer(), &bgRect);

            // Progress fill
            float progress = videoGenerator.getProgress();
            VideoGenState vstate = videoGenerator.getState();
            if (vstate == VideoGenState::Completed) {
                SDL_SetRenderDrawColor(renderer.getRenderer(), 50, 200, 50, 255);
            } else if (vstate == VideoGenState::Failed) {
                SDL_SetRenderDrawColor(renderer.getRenderer(), 200, 50, 50, 255);
            } else {
                SDL_SetRenderDrawColor(renderer.getRenderer(), 100, 150, 255, 255);
            }
            SDL_FRect fillRect = {barX, barY, barW * progress, barH};
            SDL_RenderFillRect(renderer.getRenderer(), &fillRect);

            // Border
            SDL_SetRenderDrawColor(renderer.getRenderer(), 100, 100, 100, 255);
            SDL_RenderRect(renderer.getRenderer(), &bgRect);

            // Status text (with line wrapping)
            std::string statusText = videoGenerator.getStatusText();
            char percentText[32];
            snprintf(percentText, sizeof(percentText), " (%.0f%%)", progress * 100);
            statusText += percentText;

            // Wrap text if too long (max ~80 chars per line)
            const int maxCharsPerLine = 80;
            float textY = barY + 4;
            if (statusText.length() > maxCharsPerLine) {
                std::string line1 = statusText.substr(0, maxCharsPerLine);
                std::string line2 = statusText.substr(maxCharsPerLine);
                renderer.renderText(line1.c_str(), barX + 10, textY);
                renderer.renderText(line2.c_str(), barX + 10, textY + 20);
            } else {
                renderer.renderText(statusText.c_str(), barX + 10, textY);
            }
        }

        // Analysis window
        if (showAnalysisWindow) {
            // Window dimensions
            float winW = 800, winH = 500;
            float winX = (1280 - winW) / 2;
            float winY = (720 - winH) / 2;

            // Draw window background
            SDL_SetRenderDrawColor(renderer.getRenderer(), 30, 30, 30, 240);
            SDL_FRect winRect = {winX, winY, winW, winH};
            SDL_RenderFillRect(renderer.getRenderer(), &winRect);
            SDL_SetRenderDrawColor(renderer.getRenderer(), 100, 100, 100, 255);
            SDL_RenderRect(renderer.getRenderer(), &winRect);

            // Window title
            const char* title = (analysisWindowType == 0) ? "Press Time Distribution" : "Realtime Press Time";
            renderer.renderLabel(title, winX + 10, winY + 10);

            // Draw chart area
            float chartX = winX + 60;
            float chartY = winY + 50;
            float chartW = winW - 100;
            float chartH = winH - 120;

            // Chart background
            SDL_SetRenderDrawColor(renderer.getRenderer(), 20, 20, 20, 255);
            SDL_FRect chartRect = {chartX, chartY, chartW, chartH};
            SDL_RenderFillRect(renderer.getRenderer(), &chartRect);

            // Draw chart content
            renderer.renderAnalysisChart(analysisResult, analysisWindowType, chartX, chartY, chartW, chartH);

            // Save button (top-right corner)
            float saveBtnX = winX + winW - 180;
            float saveBtnY = winY + 8;
            if (renderer.renderButton("Save", saveBtnX, saveBtnY, 80, 30, mouseX, mouseY, mouseClicked)) {
                std::string savePath = saveImageDialog();
                if (!savePath.empty()) {
                    renderer.saveAnalysisChart(analysisResult, analysisWindowType, savePath, 800, 400);
                    showAnalysisWindow = false;
                }
            }

            // Close button (top-right corner)
            if (renderer.renderButton("Close", saveBtnX + 90, saveBtnY, 80, 30, mouseX, mouseY, mouseClicked)) {
                showAnalysisWindow = false;
            }
        }

        // Back button
        if (renderer.renderButton("Back", 20, 20, 100, 35, mouseX, mouseY, mouseClicked)) {
            state = GameState::Menu;
        }
    }
    else if (state == GameState::Settings) {
        renderer.renderMenu();
        renderer.renderSettingsWindow(mouseX, mouseY);

        float winX = (1280 - 800) / 2;
        float winY = (720 - 500) / 2;
        float catW = 150, catH = 40;
        float catX = winX + 10;
        float catY = winY + 50;

        const char* categories[] = {"Sound", "Graphics", "Input", "Judgement", "Modifiers", "Misc"};
        for (int i = 0; i < 6; i++) {
            bool selected = (settingsCategory == static_cast<SettingsCategory>(i));
            renderer.renderCategoryButton(categories[i], catX, catY + i * (catH + 5), catW, catH, selected);
            if (mouseClicked && mouseX >= catX && mouseX <= catX + catW &&
                mouseY >= catY + i * (catH + 5) && mouseY <= catY + i * (catH + 5) + catH) {
                settingsCategory = static_cast<SettingsCategory>(i);
            }
        }

        float contentX = winX + 180;
        float contentY = winY + 60;
        float contentW = 600;
        float contentH = 390;

        // Reset scroll when changing category
        static SettingsCategory lastCategory = settingsCategory;
        if (lastCategory != settingsCategory) {
            settingsScroll = 0;
            lastCategory = settingsCategory;
        }

        // Set clip rect for scrollable content area
        SDL_Rect clipRect = {(int)contentX, (int)contentY, (int)contentW, (int)contentH};
        SDL_SetRenderClipRect(renderer.getRenderer(), &clipRect);

        // Apply scroll offset to contentY
        float scrolledY = contentY - settingsScroll;

        if (settingsCategory == SettingsCategory::Sound) {
            auto devices = audio.getAudioDevices();
            std::vector<const char*> deviceNames;
            for (const auto& d : devices) deviceNames.push_back(d.c_str());
            settings.audioDevice = renderer.renderDropdown("Output Device", deviceNames.data(), (int)deviceNames.size(),
                                                            settings.audioDevice, contentX, scrolledY, 300, mouseX, mouseY, mouseClicked, dropdownExpanded);

            renderer.renderLabel("Volume", contentX, scrolledY + 80);
            settings.volume = renderer.renderSliderWithValue(contentX + 80, scrolledY + 80, 200, settings.volume, 0, 100, mouseX, mouseY, mouseDown);
            audio.setVolume(settings.volume);

            renderer.renderLabel("Audio Offset", contentX, scrolledY + 120);
            settings.audioOffset = renderer.renderSliderWithValue(contentX + 120, scrolledY + 120, 200, settings.audioOffset, -300, 300, mouseX, mouseY, mouseDown);
            char offsetStr[16];
            snprintf(offsetStr, sizeof(offsetStr), "%dms", settings.audioOffset);
            renderer.renderLabel(offsetStr, contentX + 330, scrolledY + 120);
        }
        else if (settingsCategory == SettingsCategory::Input) {
            // Key count dropdown
            const char* keyCounts[] = {"1K", "2K", "3K", "4K", "5K", "6K", "7K", "8K", "9K", "10K"};
            int oldKeyCount = settings.selectedKeyCount;
            settings.selectedKeyCount = renderer.renderDropdown(nullptr, keyCounts, 10,
                settings.selectedKeyCount - 1, contentX, scrolledY, 80, mouseX, mouseY, mouseClicked, keyCountDropdownExpanded) + 1;

            // Update defaults when key count changes
            if (oldKeyCount != settings.selectedKeyCount) {
                // When switching to 8K, reset to default N+1 Mirror mode
                if (settings.selectedKeyCount == 8 && oldKeyCount != 8) {
                    settings.n1Style = true;
                    settings.mirror = true;
                }
                settings.setDefaultColors(settings.selectedKeyCount);
            }

            // Set Keys button
            if (renderer.renderButton("Set Keys", contentX + 100, scrolledY, 100, 35, mouseX, mouseY, mouseClicked)) {
                keyBindingIndex = 0;
                renderer.setKeyCount(settings.selectedKeyCount);
                state = GameState::KeyBinding;
            }

            // 8K special options: N+1 Style and Mirror
            if (settings.selectedKeyCount == 8) {
                bool oldN1 = settings.n1Style;
                bool oldMirror = settings.mirror;

                if (renderer.renderCheckbox("N+1 Style", settings.n1Style, contentX + 220, scrolledY, mouseX, mouseY, mouseClicked)) {
                    settings.n1Style = !settings.n1Style;
                }

                if (settings.n1Style) {
                    if (renderer.renderCheckbox("Mirror", settings.mirror, contentX + 340, scrolledY, mouseX, mouseY, mouseClicked)) {
                        settings.mirror = !settings.mirror;
                    }
                }

                // Handle N+1/Mirror changes
                if (oldMirror != settings.mirror) {
                    // Mirror changed: shift colors
                    if (settings.mirror) {
                        // false->true: move lane 0 to lane 7, shift 1-7 left
                        NoteColor first = settings.laneColors[0];
                        for (int i = 0; i < 7; i++) {
                            settings.laneColors[i] = settings.laneColors[i + 1];
                        }
                        settings.laneColors[7] = first;
                    } else {
                        // true->false: move lane 7 to lane 0, shift 0-6 right
                        NoteColor last = settings.laneColors[7];
                        for (int i = 7; i > 0; i--) {
                            settings.laneColors[i] = settings.laneColors[i - 1];
                        }
                        settings.laneColors[0] = last;
                    }
                } else if (oldN1 != settings.n1Style) {
                    settings.setDefaultColors(8);
                }
            }

            // Lane color settings (to the right of dropdown)
            if (!keyCountDropdownExpanded) {
                float colorX = contentX + 250;  // Right side of dropdown
                float colorY = scrolledY;
                renderer.renderLabel("Lane Colors:", colorX, colorY);
                colorY += 30;

                // Lane numbers row
                float colorBoxSize = 30;
                float colorSpacing = 35;
                for (int i = 0; i < settings.selectedKeyCount; i++) {
                    char numBuf[4];
                    snprintf(numBuf, sizeof(numBuf), "%d", i + 1);
                    renderer.renderText(numBuf, colorX + i * colorSpacing + 8, colorY);
                }
                colorY += 25;

                // Color boxes row
                for (int i = 0; i < settings.selectedKeyCount; i++) {
                    float boxX = colorX + i * colorSpacing;
                    if (renderer.renderColorBox(settings.laneColors[i], boxX, colorY, colorBoxSize, mouseX, mouseY, mouseClicked)) {
                        int colorVal = static_cast<int>(settings.laneColors[i]);
                        colorVal = (colorVal + 1) % 4;
                        settings.laneColors[i] = static_cast<NoteColor>(colorVal);
                    }
                }
            }
        }
        else if (settingsCategory == SettingsCategory::Graphics) {
            const char* resolutions[] = {"1280x720", "1920x1080", "2560x1440"};
            int oldRes = settings.resolution;
            settings.resolution = renderer.renderDropdown("Resolution", resolutions, 3, settings.resolution,
                                                           contentX, scrolledY, 180, mouseX, mouseY, mouseClicked, resolutionDropdownExpanded);
            if (oldRes != settings.resolution) {
                int widths[] = {1280, 1920, 2560};
                int heights[] = {720, 1080, 1440};
                renderer.setResolution(widths[settings.resolution], heights[settings.resolution]);
            }

            const char* refreshRates[] = {"30 FPS", "60 FPS", "120 FPS", "200 FPS", "1000 FPS"};
            int oldRefresh = settings.refreshRate;
            settings.refreshRate = renderer.renderDropdown("Refresh Rate", refreshRates, 5, settings.refreshRate,
                                                            contentX + 220, scrolledY, 150, mouseX, mouseY, mouseClicked, refreshRateDropdownExpanded);
            if (oldRefresh != settings.refreshRate) {
                int delays[] = {33, 16, 8, 5, 1};
                targetFrameDelay = delays[settings.refreshRate];
            }

            float row2Y = scrolledY + 210;
            if (renderer.renderCheckbox("V-Sync", settings.vsync, contentX, row2Y, mouseX, mouseY, mouseClicked)) {
                settings.vsync = !settings.vsync;
                renderer.setVSync(settings.vsync);
            }
            if (renderer.renderCheckbox("Borderless Fullscreen", settings.borderlessFullscreen, contentX + 120, row2Y, mouseX, mouseY, mouseClicked)) {
                settings.borderlessFullscreen = !settings.borderlessFullscreen;
                renderer.setBorderlessFullscreen(settings.borderlessFullscreen);
            }

            float row3Y = row2Y + 40;
            renderer.renderLabel("Quality", contentX, row3Y);
            if (renderer.renderRadioButton("Low", settings.quality == 0, contentX + 80, row3Y, mouseX, mouseY, mouseClicked)) {
                settings.quality = 0;
            }
            if (renderer.renderRadioButton("Medium", settings.quality == 1, contentX + 150, row3Y, mouseX, mouseY, mouseClicked)) {
                settings.quality = 1;
            }
            if (renderer.renderRadioButton("High", settings.quality == 2, contentX + 250, row3Y, mouseX, mouseY, mouseClicked)) {
                settings.quality = 2;
            }

            if (renderer.renderCheckbox("Low Spec Mode", settings.lowSpecMode, contentX, row3Y + 40, mouseX, mouseY, mouseClicked)) {
                settings.lowSpecMode = !settings.lowSpecMode;
            }
            if (renderer.renderCheckbox("Lane Light", settings.laneLight, contentX + 180, row3Y + 40, mouseX, mouseY, mouseClicked)) {
                settings.laneLight = !settings.laneLight;
            }

            // Scroll Speed settings
            float row4Y = row3Y + 80;
            renderer.renderLabel("Scroll Speed (1-40)", contentX, row4Y);

            // Text input for scroll speed (to the right of label, same line)
            bool wasEditing = editingScrollSpeed;
            renderer.renderTextInput(nullptr, scrollSpeedInput, contentX + 210, row4Y - 5, 50, mouseX, mouseY, mouseClicked, editingScrollSpeed, settingsCursorPos);

            // Handle text input start/stop
            if (editingScrollSpeed && !wasEditing) {
                // Just started editing - initialize with current value
                scrollSpeedInput = std::to_string(settings.scrollSpeed);
                SDL_StartTextInput(renderer.getWindow());
            } else if (!editingScrollSpeed && wasEditing) {
                SDL_StopTextInput(renderer.getWindow());
            }

            // Initialize input string if not editing
            if (!editingScrollSpeed && scrollSpeedInput.empty()) {
                scrollSpeedInput = std::to_string(settings.scrollSpeed);
            }

            // Parse and apply scroll speed when not editing
            if (!editingScrollSpeed && !scrollSpeedInput.empty()) {
                try {
                    int newSpeed = std::stoi(scrollSpeedInput);
                    if (settings.unlimitedSpeed) {
                        settings.scrollSpeed = std::max(1, newSpeed);
                    } else {
                        settings.scrollSpeed = std::max(1, std::min(40, newSpeed));
                    }
                    scrollSpeedInput = std::to_string(settings.scrollSpeed);
                } catch (...) {
                    scrollSpeedInput = std::to_string(settings.scrollSpeed);
                }
            }

            // BPM Scale checkbox (to the right of input)
            if (renderer.renderCheckbox("BPM Scale", settings.bpmScaleMode, contentX + 280, row4Y, mouseX, mouseY, mouseClicked)) {
                settings.bpmScaleMode = !settings.bpmScaleMode;
            }

            // Fun Mode checkbox (to the right of BPM Scale) - unlocks scroll speed limit
            if (renderer.renderCheckbox("Fun Mode", settings.unlimitedSpeed, contentX + 420, row4Y, mouseX, mouseY, mouseClicked)) {
                settings.unlimitedSpeed = !settings.unlimitedSpeed;
            }

            // Skin selection
            float row5Y = row4Y + 40;
            renderer.renderLabel("Skin", contentX, row5Y);
            std::string skinDisplay = settings.skinPath.empty() ? "Default" :
                settings.skinPath.substr(settings.skinPath.find_last_of("/\\") + 1);
            renderer.renderText(skinDisplay.c_str(), contentX + 60, row5Y);
            if (renderer.renderButton("Browse", contentX + 250, row5Y - 5, 80, 30, mouseX, mouseY, mouseClicked)) {
                // Open folder dialog for skin selection
                std::string skinFolder = openSkinFolderDialog();
                if (!skinFolder.empty()) {
                    settings.skinPath = skinFolder;
                    skinManager.loadSkin(skinFolder, renderer.getRenderer());
                }
            }
            if (!settings.skinPath.empty()) {
                if (renderer.renderButton("Reset", contentX + 340, row5Y - 5, 60, 30, mouseX, mouseY, mouseClicked)) {
                    settings.skinPath = "";
                    skinManager.unloadSkin();
                }
            }

            // Background Dim slider
            float row6Y = row5Y + 40;
            renderer.renderLabel("Background Dim", contentX, row6Y);
            settings.backgroundDim = renderer.renderSliderWithValue(contentX + 150, row6Y, 200,
                settings.backgroundDim, 0, 100, mouseX, mouseY, mouseDown);
        }
        else if (settingsCategory == SettingsCategory::Judgement) {
            const char* judgeModes[] = {"Beatmap OD", "Custom OD", "Custom Windows", "O2Jam"};
            int modeIdx = static_cast<int>(settings.judgeMode);
            int newMode = renderer.renderDropdown("Judge Mode", judgeModes, 4, modeIdx, contentX, scrolledY, 200, mouseX, mouseY, mouseClicked, judgeModeDropdownExpanded);
            settings.judgeMode = static_cast<JudgementMode>(newMode);

            if (renderer.renderCheckbox("NoteLock", settings.noteLock, contentX + 220, scrolledY, mouseX, mouseY, mouseClicked)) {
                settings.noteLock = !settings.noteLock;
            }

            float modeContentY = scrolledY + 150;
            if (settings.judgeMode == JudgementMode::CustomOD) {
                renderer.renderLabel("OD Value", contentX, modeContentY);
                int odInt = (int)(settings.customOD * 10);
                int maxOD = settings.funMode ? 9990 : 120;
                odInt = renderer.renderSliderWithFloatValue(contentX + 100, modeContentY, 200, odInt, 0, maxOD, 10.0f, mouseX, mouseY, mouseDown);
                settings.customOD = odInt / 10.0f;
                if (renderer.renderCheckbox("Fun Mode", settings.funMode, contentX, modeContentY + 35, mouseX, mouseY, mouseClicked)) {
                    settings.funMode = !settings.funMode;
                }
            }
            else if (settings.judgeMode == JudgementMode::CustomWindows) {
                const char* judgeNames[] = {"300g", "300", "200", "100", "50", "Miss"};
                for (int i = 0; i < 6; i++) {
                    float rowY = modeContentY + i * 30;
                    if (renderer.renderClickableLabel(judgeNames[i], contentX, rowY, mouseX, mouseY, mouseClicked)) {
                        judgeDetailPopup = (judgeDetailPopup == i) ? -1 : i;
                    }
                    int val = (int)settings.judgements[i].window;
                    val = renderer.renderSliderWithValue(contentX + 60, rowY, 150, val, 0, 300, mouseX, mouseY, mouseDown);
                    settings.judgements[i].window = val;
                }

                if (judgeDetailPopup >= 0 && judgeDetailPopup < 6) {
                    float popX = contentX + 280;
                    float popY = modeContentY;
                    renderer.renderPopupWindow(popX, popY, 220, 130);

                    renderer.renderText(judgeNames[judgeDetailPopup], popX + 10, popY + 10);

                    if (renderer.renderCheckbox("No Combo Break", !settings.judgements[judgeDetailPopup].breaksCombo,
                                                 popX + 10, popY + 40, mouseX, mouseY, mouseClicked)) {
                        settings.judgements[judgeDetailPopup].breaksCombo = !settings.judgements[judgeDetailPopup].breaksCombo;
                    }

                    if (renderer.renderCheckbox("Enabled", settings.judgements[judgeDetailPopup].enabled,
                                                 popX + 10, popY + 70, mouseX, mouseY, mouseClicked)) {
                        settings.judgements[judgeDetailPopup].enabled = !settings.judgements[judgeDetailPopup].enabled;
                    }

                    char accBuf[16];
                    snprintf(accBuf, sizeof(accBuf), "Acc: %.1f%%", settings.judgements[judgeDetailPopup].accuracy);
                    renderer.renderText(accBuf, popX + 10, popY + 100);
                }
            }

            // Hit Error Bar Scale slider (at bottom)
            float errorBarY = modeContentY + 200;
            renderer.renderLabel("Hit Error Bar Scale", contentX, errorBarY);
            int scaleInt = (int)(settings.hitErrorBarScale * 100);
            scaleInt = renderer.renderSliderWithFloatValue(contentX + 150, errorBarY, 150, scaleInt, 50, 300, 100.0f, mouseX, mouseY, mouseDown);
            settings.hitErrorBarScale = scaleInt / 100.0f;
        }
        else if (settingsCategory == SettingsCategory::Modifiers) {
            renderer.renderLabel("Game Modifiers", contentX, scrolledY);
            if (renderer.renderCheckbox("AutoPlay", settings.autoPlayEnabled,
                                         contentX, scrolledY + 30, mouseX, mouseY, mouseClicked)) {
                settings.autoPlayEnabled = !settings.autoPlayEnabled;
            }
            if (renderer.renderCheckbox("Hidden", settings.hiddenEnabled,
                                         contentX, scrolledY + 60, mouseX, mouseY, mouseClicked)) {
                settings.hiddenEnabled = !settings.hiddenEnabled;
                // Hidden and FadeIn are mutually exclusive
                if (settings.hiddenEnabled) settings.fadeInEnabled = false;
            }
            renderer.renderText("Notes fade out near judge line", contentX + 20, scrolledY + 90);
            if (renderer.renderCheckbox("FadeIn", settings.fadeInEnabled,
                                         contentX, scrolledY + 120, mouseX, mouseY, mouseClicked)) {
                settings.fadeInEnabled = !settings.fadeInEnabled;
                // Hidden and FadeIn are mutually exclusive
                if (settings.fadeInEnabled) settings.hiddenEnabled = false;
            }
            renderer.renderText("Notes fade in from top", contentX + 20, scrolledY + 150);
            if (renderer.renderCheckbox("IgnoreSV", settings.ignoreSV,
                                         contentX, scrolledY + 180, mouseX, mouseY, mouseClicked)) {
                settings.ignoreSV = !settings.ignoreSV;
            }
            renderer.renderText("Ignore scroll velocity changes", contentX + 20, scrolledY + 210);
            if (renderer.renderCheckbox("Death", settings.deathEnabled,
                                         contentX, scrolledY + 240, mouseX, mouseY, mouseClicked)) {
                settings.deathEnabled = !settings.deathEnabled;
            }
            renderer.renderText("HP=0 causes death", contentX + 20, scrolledY + 270);
            if (renderer.renderCheckbox("Sudden Death", settings.suddenDeathEnabled,
                                         contentX, scrolledY + 300, mouseX, mouseY, mouseClicked)) {
                settings.suddenDeathEnabled = !settings.suddenDeathEnabled;
            }
            renderer.renderText("Any miss causes instant death", contentX + 20, scrolledY + 330);

            // Speed modifiers
            renderer.renderLabel("Speed Modifiers", contentX, scrolledY + 370);
            if (renderer.renderCheckbox("Double Time", settings.doubleTimeEnabled,
                                         contentX, scrolledY + 400, mouseX, mouseY, mouseClicked)) {
                settings.doubleTimeEnabled = !settings.doubleTimeEnabled;
                if (settings.doubleTimeEnabled) settings.halfTimeEnabled = false;
            }
            renderer.renderText("1.5x speed", contentX + 20, scrolledY + 430);

            if (renderer.renderCheckbox("Nightcore", settings.nightcoreEnabled,
                                         contentX, scrolledY + 460, mouseX, mouseY, mouseClicked)) {
                settings.nightcoreEnabled = !settings.nightcoreEnabled;
                if (settings.nightcoreEnabled) {
                    settings.doubleTimeEnabled = true;  // NC auto-enables DT
                    settings.halfTimeEnabled = false;
                }
            }
            renderer.renderText("1.5x speed + pitch up", contentX + 20, scrolledY + 490);

            if (renderer.renderCheckbox("Half Time", settings.halfTimeEnabled,
                                         contentX, scrolledY + 520, mouseX, mouseY, mouseClicked)) {
                settings.halfTimeEnabled = !settings.halfTimeEnabled;
                if (settings.halfTimeEnabled) {
                    settings.doubleTimeEnabled = false;
                    settings.nightcoreEnabled = false;
                }
            }
            renderer.renderText("0.75x speed, 0.5x score", contentX + 20, scrolledY + 550);
        }
        else if (settingsCategory == SettingsCategory::Misc) {
            renderer.renderLabel("Hold Note Judgement", contentX, scrolledY);
            if (renderer.renderCheckbox("ScoreV1", settings.legacyHoldJudgement,
                                         contentX, scrolledY + 30, mouseX, mouseY, mouseClicked)) {
                settings.legacyHoldJudgement = !settings.legacyHoldJudgement;
            }
            renderer.renderText("Check: ScoreV1 algorithm", contentX + 20, scrolledY + 60);
            renderer.renderText("Uncheck: ScoreV2 algorithm", contentX + 20, scrolledY + 80);

            // Username input
            float usernameY = scrolledY + 120;
            static bool wasEditingUsername = false;
            renderer.renderTextInput("Username", settings.username, contentX, usernameY, 200,
                                     mouseX, mouseY, mouseClicked, editingUsername, settingsCursorPos);
            if (editingUsername && !wasEditingUsername) {
                SDL_StartTextInput(renderer.getWindow());
            } else if (!editingUsername && wasEditingUsername) {
                SDL_StopTextInput(renderer.getWindow());
            }
            wasEditingUsername = editingUsername;

            // Force override checkbox (to the right of username input)
            if (renderer.renderCheckbox("Force Override When Exporting", settings.forceOverrideUsername,
                                         contentX + 220, usernameY + 25, mouseX, mouseY, mouseClicked)) {
                settings.forceOverrideUsername = !settings.forceOverrideUsername;
            }

            // Debug mode checkbox
            float debugY = usernameY + 70;
            renderer.renderLabel("Debug", contentX, debugY);
            if (renderer.renderCheckbox("Enable Debug Logging", settings.debugEnabled,
                                         contentX, debugY + 30, mouseX, mouseY, mouseClicked)) {
                settings.debugEnabled = !settings.debugEnabled;
#ifdef _WIN32
                if (settings.debugEnabled) {
                    AllocConsole();
                    freopen("CONOUT$", "w", stdout);
                    freopen("CONOUT$", "w", stderr);
                } else {
                    FreeConsole();
                }
#endif
            }
            renderer.renderText("When enabled, Export Log button appears in result screen", contentX + 20, debugY + 60);

            float skinY = debugY + 100;
            renderer.renderLabel("Skin", contentX, skinY);
            if (renderer.renderCheckbox("Ignore Beatmap Skin", settings.ignoreBeatmapSkin,
                                         contentX, skinY + 30, mouseX, mouseY, mouseClicked)) {
                settings.ignoreBeatmapSkin = !settings.ignoreBeatmapSkin;
            }
            if (renderer.renderCheckbox("Ignore Beatmap Hitsounds", settings.ignoreBeatmapHitsounds,
                                         contentX + 250, skinY + 30, mouseX, mouseY, mouseClicked)) {
                settings.ignoreBeatmapHitsounds = !settings.ignoreBeatmapHitsounds;
            }
            if (renderer.renderCheckbox("Disable Storyboard", settings.disableStoryboard,
                                         contentX, skinY + 60, mouseX, mouseY, mouseClicked)) {
                settings.disableStoryboard = !settings.disableStoryboard;
            }

            // Star Rating Version dropdown
            float starY = skinY + 110;
            renderer.renderLabel("Star Rating Version", contentX, starY);
            const char* starVersions[] = {"b20260101", "b20220101"};
            settings.starRatingVersion = renderer.renderDropdown(nullptr, starVersions, 2,
                settings.starRatingVersion, contentX, starY + 30, 150, mouseX, mouseY, mouseClicked, starRatingDropdownExpanded);
        }

        // Reset clip rect before rendering Close button
        SDL_SetRenderClipRect(renderer.getRenderer(), nullptr);

        // Clear Index button
        if (renderer.renderButton("Clear Index", winX + 800 - 208, winY + 500 - 45, 116, 30, mouseX, mouseY, mouseClicked)) {
            // Delete all index files
            std::string indexDir = SongIndex::getIndexDir();
            if (std::filesystem::exists(indexDir)) {
                std::filesystem::remove_all(indexDir);
            }
            // Rebuild index immediately
            songList.clear();
            scanSongsFolder();
        }

        if (renderer.renderButton("Close", winX + 800 - 80, winY + 500 - 45, 60, 30, mouseX, mouseY, mouseClicked)) {
            saveConfig();
            state = GameState::Menu;
        }
    }
    else if (state == GameState::KeyBinding) {
        renderer.renderLanes();
        renderer.renderStageBottom();
        renderer.renderJudgeLine();
        renderer.renderStageBorders();
        renderer.renderKeys(laneKeyDown, settings.selectedKeyCount, SDL_GetTicks());
        renderer.renderKeyBindingUI(settings.keys[settings.selectedKeyCount - 1], settings.selectedKeyCount, keyBindingIndex);
    }
    else if (state == GameState::Playing || state == GameState::Paused || state == GameState::Dead) {
        int64_t elapsed = SDL_GetTicks() - startTime;
        // If paused/dead, use frozen time to stop the display
        if (state == GameState::Paused) {
            elapsed = pauseTime - startTime;
        } else if (state == GameState::Dead) {
            elapsed = deathTime - startTime;
        }
        // For keysound-only maps, use system time instead of audio position
        int64_t currentTime;
        if (state == GameState::Paused || pauseFadingOut) {
            // Use saved game time when paused or during fade out
            currentTime = pauseGameTime;
        } else if (!musicStarted) {
            currentTime = static_cast<int64_t>((elapsed - PREPARE_TIME) * clockRate);
        } else if (hasBackgroundMusic) {
            // When dead, use frozen time instead of audio position
            if (state == GameState::Dead) {
                currentTime = static_cast<int64_t>((elapsed - PREPARE_TIME) * clockRate);
            } else {
                currentTime = audio.getPosition() + settings.audioOffset + pauseAudioOffset;
            }
        } else {
            currentTime = static_cast<int64_t>((elapsed - PREPARE_TIME) * clockRate);
        }

        // Update and render storyboard background
        bool isPassing = hpManager.getHPPercent() > 0;

        // Notify storyboard of passing state change for triggers
        if (isPassing != lastStoryboardPassing) {
            storyboard.onPassingChanged(isPassing, currentTime);
            lastStoryboardPassing = isPassing;
        }

        storyboard.update(currentTime, isPassing);
        // Use currentBgTexture for O2Jam/DJMAX Online (already loaded in song select)
        if (!storyboard.hasBackground() && currentBgTexture) {
            SDL_FRect bgRect = {0, 0, 1280, 720};
            SDL_RenderTexture(renderer.getRenderer(), currentBgTexture, nullptr, &bgRect);
        } else {
            storyboard.renderBackground(renderer.getRenderer());
        }

        // Render BGA if available (DJMAX Online or BMS)
        if (hasBga) {
            static bool bgaDebugOnce = false;
            if (!bgaDebugOnce) {
                std::cout << "BGA: First frame, calling updateBga/renderBga..." << std::endl;
                bgaDebugOnce = true;
            }
            if (isBmsBga) {
                bmsBgaManager.update(currentTime, clockRate);
                bmsBgaManager.render(0, 0, 1280, 720);
            } else {
                updateBga(currentTime);
                renderBga();
            }
        }

        // Skip storyboard render for BMS BGA to avoid covering it
        if (!isBmsBga) {
            storyboard.render(renderer.getRenderer(), StoryboardLayer::Background, isPassing);
        }

        // Apply background dim
        if (settings.backgroundDim > 0) {
            int winW, winH;
            SDL_GetWindowSize(renderer.getWindow(), &winW, &winH);
            SDL_SetRenderDrawBlendMode(renderer.getRenderer(), SDL_BLENDMODE_BLEND);
            int dimAlpha = (int)(settings.backgroundDim * 255 / 100);
            SDL_SetRenderDrawColor(renderer.getRenderer(), 0, 0, 0, dimAlpha);
            SDL_FRect dimRect = {0, 0, (float)winW, (float)winH};
            SDL_RenderFillRect(renderer.getRenderer(), &dimRect);
        }

        // Layer 1: Background (lanes)
        renderer.renderLanes();

        // Layer 2: Lane light effect (below notes)
        if (settings.laneLight) {
            // Replay mode ignores Hidden/FadeIn mods
            bool useHidden = settings.hiddenEnabled && !replayMode;
            bool useFadeIn = settings.fadeInEnabled && !replayMode;
            renderer.renderLaneHighlights(laneKeyDown, beatmap.keyCount, useHidden, useFadeIn, combo);
        }

        // Layer 3: mania-stage-light (below notes, stage bottom, keys)
        renderer.renderHitLighting(laneKeyDown, beatmap.keyCount);

        // Check if skin has custom keyImage
        bool hasCustomKeys = skinManager.hasCustomKeyImage(beatmap.keyCount);

        // Build laneHoldActive array for LightingL
        bool laneHoldActive[18] = {false};
        for (const auto& note : beatmap.notes) {
            if (note.state == NoteState::Holding && note.lane < 18) {
                // Only show LightingL if user is actually holding
                if (laneKeyDown[note.lane]) {
                    laneHoldActive[note.lane] = true;
                }
            }
        }

        // Track if any hold is active for combo color
        bool wasHoldActive = anyHoldActive;
        anyHoldActive = false;
        for (int i = 0; i < 18; i++) {
            if (laneHoldActive[i]) {
                anyHoldActive = true;
                break;
            }
        }
        // Detect hold state change for color transition
        if (anyHoldActive != wasHoldActive) {
            holdColorChangeTime = now;
        }

        // Replay mode ignores Hidden/FadeIn mods
        bool useHidden = settings.hiddenEnabled && !replayMode;
        bool useFadeIn = settings.fadeInEnabled && !replayMode;

        if (hasCustomKeys) {
            // Custom keyImage: Keys below notes
            renderer.renderKeys(laneKeyDown, beatmap.keyCount, currentTime);
            renderer.renderNotes(beatmap.notes, currentTime, settings.scrollSpeed, baseBPM, settings.bpmScaleMode, beatmap.timingPoints, settings.laneColors, useHidden, useFadeIn, combo, settings.ignoreSV, clockRate);
        } else {
            // Default keyImage: Notes below keys
            renderer.renderNotes(beatmap.notes, currentTime, settings.scrollSpeed, baseBPM, settings.bpmScaleMode, beatmap.timingPoints, settings.laneColors, useHidden, useFadeIn, combo, settings.ignoreSV, clockRate);
            renderer.renderKeys(laneKeyDown, beatmap.keyCount, currentTime);
        }

        // Layer 5: Stage bottom (above notes)
        renderer.renderStageBottom();
        renderer.renderJudgeLine();

        // Layer 6: LightingN/LightingL (above stage bottom)
        renderer.renderNoteLighting(laneHoldActive, beatmap.keyCount, currentTime);

        // Layer 7: Stage borders
        renderer.renderStageBorders();

        // Storyboard Foreground layer
        storyboard.render(renderer.getRenderer(), StoryboardLayer::Foreground, isPassing);

        // Judgement animation (below Overlay)
        int64_t judgementElapsed = now - lastJudgementTime;
        if (judgementElapsed < 220) {
            renderer.renderHitJudgement(lastJudgementIndex, judgementElapsed);
        }

        // HP bar (below Overlay)
        renderer.renderHPBar(hpManager.getHPPercent());

        // Storyboard Overlay layer
        storyboard.render(renderer.getRenderer(), StoryboardLayer::Overlay, isPassing);

        // UI elements (above Overlay)
        renderer.renderSpeedInfo(settings.scrollSpeed, settings.bpmScaleMode, autoPlay, settings.autoPlayEnabled);
        if (replayMode) {
            renderer.renderText("[REPLAY]", 20, 30);
            renderer.renderScorePanel(replayInfo.playerName.c_str(), score, calculateAccuracy(), maxCombo);
        } else {
            renderer.renderScorePanel(settings.username.c_str(), score, calculateAccuracy(), maxCombo);
        }
        // Hide hit error bar in O2Jam mode (overlap-based judgement)
        if (settings.judgeMode != JudgementMode::O2Jam) {
            renderer.renderHitErrorBar(hitErrors, SDL_GetTicks(),
                judgementSystem.getMarvelousWindow(), judgementSystem.getPerfectWindow(),
                judgementSystem.getGreatWindow(), judgementSystem.getGoodWindow(),
                judgementSystem.getBadWindow(), judgementSystem.getMissWindow(),
                judgementSystem.getEnabledArray(), settings.hitErrorBarScale);
        }
        renderer.renderFPS(fps);
        renderer.renderGameInfo(currentTime, totalTime, judgementCounts, calculateAccuracy(), score);

        // Performance monitoring (debug mode only)
        if (settings.debugEnabled) {
            char perfText[128];

            // Judgement windows display
            if (settings.judgeMode == JudgementMode::O2Jam) {
                double hiSpeed = settings.scrollSpeed / 3.657;
                snprintf(perfText, sizeof(perfText), "Windows: Hi-Speed X%.2f", hiSpeed);
            } else {
                // Build windows string, showing "-" for disabled judgements
                std::string windowsStr = "Windows: ";
                const bool* enabled = judgementSystem.getEnabledArray();
                double windows[6] = {
                    judgementSystem.getMarvelousWindow(), judgementSystem.getPerfectWindow(),
                    judgementSystem.getGreatWindow(), judgementSystem.getGoodWindow(),
                    judgementSystem.getBadWindow(), judgementSystem.getMissWindow()
                };
                for (int i = 0; i < 6; i++) {
                    if (i > 0) windowsStr += "/";
                    if (enabled[i]) {
                        char buf[16];
                        snprintf(buf, sizeof(buf), "%.1f", windows[i]);
                        windowsStr += buf;
                    } else {
                        windowsStr += "-";
                    }
                }
                strncpy(perfText, windowsStr.c_str(), sizeof(perfText) - 1);
                perfText[sizeof(perfText) - 1] = '\0';
            }
            renderer.renderText(perfText, 20, 575);

            snprintf(perfText, sizeof(perfText), "Input: %.2fms", perfInput);
            renderer.renderText(perfText, 20, 595);
            snprintf(perfText, sizeof(perfText), "Update: %.2fms", perfUpdate);
            renderer.renderText(perfText, 20, 615);
            snprintf(perfText, sizeof(perfText), "Draw: %.2fms", perfDraw);
            renderer.renderText(perfText, 20, 635);
        }

        // Star rating display (above PP)
        char starText[32];
        snprintf(starText, sizeof(starText), "Star: %.2f", currentStarRating);
        renderer.renderText(starText, 20, 655);

        // PP display (bottom left of play area)
        std::string ppText = std::to_string(ppCalculator.getCurrentPP()) + " PP";
        renderer.renderText(ppText.c_str(), 20, 680);

        // Combo (above Overlay)
        int64_t comboAnimTime = now - lastComboChangeTime;
        int64_t breakAnimTime = now - comboBreakTime;
        int64_t holdColorElapsed = now - holdColorChangeTime;
        renderer.renderCombo(combo, comboAnimTime, comboBreak, breakAnimTime, lastComboValue, anyHoldActive, holdColorElapsed);

        // Menus (topmost)
        if (state == GameState::Paused) {
            // Fade in: 1 second
            int64_t elapsed = SDL_GetTicks() - pauseTime;
            float alpha = std::min(1.0f, elapsed / 1000.0f);
            renderer.renderPauseMenu(pauseMenuSelection, alpha);
        }

        // Pause fade out overlay (when resuming)
        if (pauseFadingOut && state == GameState::Playing) {
            int64_t elapsed = SDL_GetTicks() - pauseFadeOutStart;
            float alpha = 1.0f - std::min(1.0f, elapsed / 2000.0f);  // 2 second fade out
            if (alpha > 0) {
                renderer.renderPauseMenu(pauseMenuSelection, alpha);
            } else {
                // Fade out complete, now adjust time and resume audio
                pauseFadingOut = false;
                // If music hadn't started yet (paused during prepare time), just adjust startTime
                if (!musicStarted) {
                    int64_t totalPausedDuration = SDL_GetTicks() - pauseTime;
                    startTime += totalPausedDuration;
                } else if (hasBackgroundMusic) {
                    // For BGM maps, adjust startTime now
                    int64_t totalPausedDuration = SDL_GetTicks() - pauseTime;
                    startTime += totalPausedDuration;
                    pauseAudioOffset = pauseGameTime - (audio.getPosition() + settings.audioOffset);
                    audio.resume();
                    audio.resumeAllSamples();
                } else {
                    // For keysound-only maps, add fadeout duration to startTime
                    int64_t fadeoutDuration = SDL_GetTicks() - pauseFadeOutStart;
                    startTime += fadeoutDuration;
                    audio.resume();
                    audio.resumeAllSamples();
                }
            }
        }

        if (state == GameState::Dead) {
            // Fade in: 1 second
            int64_t elapsed = SDL_GetTicks() - deathTime;
            float alpha = std::min(1.0f, elapsed / 1000.0f);
            renderer.renderDeathMenu(deathMenuSelection, deathSlowdown, alpha);
        }

        // Skip prompt
        if (canSkip && state == GameState::Playing) {
            renderer.renderSkipPrompt();
        }

        if (showEndPrompt) {
            renderer.renderText("Press Enter to finish", 1280/2 - 100, 400);
        }
    }

    renderer.present();
}

Judgement Game::checkJudgement(int lane, int64_t atTime) {
    int64_t currentTime;
    if (atTime != INT64_MIN) {
        currentTime = atTime;
    } else {
        currentTime = getCurrentGameTime();
    }

    addDebugLog(currentTime, "KEY_DOWN", lane, "");

    // First check for Released hold notes that can be recovered
    // O2Jam mode: hold notes cannot be recovered once released
    if (settings.judgeMode != JudgementMode::O2Jam) {
        for (auto& note : beatmap.notes) {
            if (note.isFakeNote) continue;  // Skip fake notes
            if (note.state == NoteState::Released && note.lane == lane && note.isHold) {
                // Recover the hold note
                // osu! DOES reset tick time on recovery (method_12 sets int_10 = currentTime)
                note.state = NoteState::Holding;
                note.nextTickTime = currentTime;  // Reset tick timer on recovery
                // DO NOT reset headReleaseTime - head should keep falling
                SDL_Log("HOLD_RECOVER: lane=%d headReleaseTime=%lld", lane, (long long)note.headReleaseTime);
                addDebugLog(currentTime, "HOLD_RECOVER", lane,
                    "noteTime=" + std::to_string(note.time) + " endTime=" + std::to_string(note.endTime) +
                    " nextTickTime=" + std::to_string(note.nextTickTime) +
                    " headReleaseTime=" + std::to_string(note.headReleaseTime));
                return Judgement::None;
            }
        }
    }

    // Then check for new notes
    for (size_t noteIdx = 0; noteIdx < beatmap.notes.size(); noteIdx++) {
        Note& note = beatmap.notes[noteIdx];
        if (note.state != NoteState::Waiting || note.lane != lane) continue;
        if (note.isFakeNote) continue;  // Skip fake notes - visual only

        int64_t diff = std::abs(note.time - currentTime);

        // Calculate if note can be hit
        bool canHit = false;
        bool isMiss = false;
        if (settings.judgeMode == JudgementMode::O2Jam) {
            // O2Jam: overlap-based judgement
            // Convert scrollSpeed to O2Jam Hi-Speed, scaled by clockRate (DT/NC/HT)
            double hiSpeed = settings.scrollSpeed / 3.657 * clockRate;
            int noteY = renderer.getNoteY(note.time, currentTime, settings.scrollSpeed, baseBPM,
                                          settings.bpmScaleMode, beatmap.timingPoints,
                                          settings.ignoreSV, clockRate);
            int judgeLineY = renderer.getJudgeLineY();
            double overlap = calcOverlapPercent(noteY, judgeLineY, Renderer::NOTE_HEIGHT, hiSpeed);
            canHit = (overlap >= 0.20);  // Can hit if overlap >= 20%
            isMiss = (overlap < 0.20 && currentTime > note.time);  // Miss if note passed and overlap < 20%
        } else {
            // Other modes: time-based judgement
            // Use max enabled window for canHit (allows custom large windows to work)
            canHit = (diff <= judgementSystem.getMaxEnabledWindow());
            // If miss is disabled, don't trigger early miss (just ignore the keypress)
            isMiss = (diff <= judgementSystem.getMissWindow() && !canHit && judgementSystem.isEnabled(5));
        }

        if (settings.noteLock) {
            // NoteLock: only hit the earliest note in this lane
            if (canHit) {
                // Play key sound
                keySoundManager.playKeySound(note, false);

                // Notify storyboard of hitsound and hit event
                storyboard.onHitSound(buildHitSoundInfo(note, beatmap.timingPoints, currentTime, false), currentTime);
                storyboard.onHitObjectHit(currentTime);

                if (note.isHold) {
                    note.state = NoteState::Holding;
                    note.headHitError = diff;
                    note.headHit = true;  // Mark head as hit
                    note.headHitEarly = (currentTime < note.time);  // Early or late hit
                    note.nextTickTime = currentTime;  // Start ticks from hit time
                    SDL_Log("HOLD_HIT: lane=%d headHitEarly=%d currentTime=%lld noteTime=%lld",
                        lane, note.headHitEarly ? 1 : 0, (long long)currentTime, (long long)note.time);
                } else {
                    note.state = NoteState::Hit;
                    SDL_Log("NOTE_HIT: lane=%d noteTime=%lld currentTime=%lld diff=%lld",
                        lane, (long long)note.time, (long long)currentTime, (long long)diff);
                    renderer.triggerLightingN(lane, currentTime);
                    Judgement j = getJudgement(diff, note.time, currentTime);
                    processJudgement(j, lane);
                }
                // Update next note index for this lane
                updateLaneNextNoteIndex(laneNextNoteIndex, beatmap.notes, lane, static_cast<int>(noteIdx));
                hitErrors.push_back({(int64_t)SDL_GetTicks(), currentTime - note.time});
                return note.isHold ? Judgement::None : getJudgement(diff, note.time, currentTime);
            }
            // Miss judgement
            if (isMiss) {
                note.state = NoteState::Missed;
                processJudgement(Judgement::Miss, lane);
                hitErrors.push_back({(int64_t)SDL_GetTicks(), currentTime - note.time});
                return Judgement::Miss;
            }
            // Outside miss window - play empty tap keysound and ignore keypress
            if (lane >= 0 && lane < 10 && laneNextNoteIndex[lane] >= 0 &&
                laneNextNoteIndex[lane] < static_cast<int>(beatmap.notes.size())) {
                const Note& nextNote = beatmap.notes[laneNextNoteIndex[lane]];
                keySoundManager.playKeySound(nextNote, false);
                // Use timing point's sampleSet for empty tap trigger matching
                storyboard.onHitSound(buildEmptyTapHitSoundInfo(beatmap.timingPoints, currentTime, nextNote), currentTime);
            }
            return Judgement::None;
        } else {
            // No NoteLock
            if (canHit) {
                // Play key sound
                keySoundManager.playKeySound(note, false);

                // Notify storyboard of hitsound and hit event
                storyboard.onHitSound(buildHitSoundInfo(note, beatmap.timingPoints, currentTime, false), currentTime);
                storyboard.onHitObjectHit(currentTime);

                if (note.isHold) {
                    note.state = NoteState::Holding;
                    note.headHitError = diff;
                    note.headHit = true;  // Mark head as hit
                    note.headHitEarly = (currentTime < note.time);  // Early or late hit
                    note.nextTickTime = currentTime;  // Start ticks from hit time
                    SDL_Log("HOLD_HIT: lane=%d headHitEarly=%d currentTime=%lld noteTime=%lld",
                        lane, note.headHitEarly ? 1 : 0, (long long)currentTime, (long long)note.time);
                } else {
                    note.state = NoteState::Hit;
                    SDL_Log("NOTE_HIT: lane=%d noteTime=%lld currentTime=%lld diff=%lld",
                        lane, (long long)note.time, (long long)currentTime, (long long)diff);
                    renderer.triggerLightingN(lane, currentTime);
                    Judgement j = getJudgement(diff, note.time, currentTime);
                    processJudgement(j, lane);
                }
                // Update next note index for this lane
                updateLaneNextNoteIndex(laneNextNoteIndex, beatmap.notes, lane, static_cast<int>(noteIdx));
                hitErrors.push_back({(int64_t)SDL_GetTicks(), currentTime - note.time});
                return note.isHold ? Judgement::None : getJudgement(diff, note.time, currentTime);
            }
            // Miss judgement
            if (isMiss) {
                note.state = NoteState::Missed;
                processJudgement(Judgement::Miss, lane);
                hitErrors.push_back({(int64_t)SDL_GetTicks(), currentTime - note.time});
                return Judgement::Miss;
            }
            // Outside miss window - play empty tap keysound and ignore keypress
            if (lane >= 0 && lane < 10 && laneNextNoteIndex[lane] >= 0 &&
                laneNextNoteIndex[lane] < static_cast<int>(beatmap.notes.size())) {
                const Note& nextNote = beatmap.notes[laneNextNoteIndex[lane]];
                keySoundManager.playKeySound(nextNote, false);
                // Use timing point's sampleSet for empty tap trigger matching
                storyboard.onHitSound(buildEmptyTapHitSoundInfo(beatmap.timingPoints, currentTime, nextNote), currentTime);
            }
            return Judgement::None;
        }
    }

    // No note hit - play empty tap keysound (next note's keysound in this lane)
    SDL_Log("Empty tap: lane=%d laneNextNoteIndex=%d", lane, laneNextNoteIndex[lane]);
    if (lane >= 0 && lane < 10 && laneNextNoteIndex[lane] >= 0 &&
        laneNextNoteIndex[lane] < static_cast<int>(beatmap.notes.size())) {
        const Note& nextNote = beatmap.notes[laneNextNoteIndex[lane]];
        SDL_Log("  Playing empty tap keysound for note at time=%lld", (long long)nextNote.time);
        keySoundManager.playKeySound(nextNote, false);
        // Use timing point's sampleSet for empty tap trigger matching
        storyboard.onHitSound(buildEmptyTapHitSoundInfo(beatmap.timingPoints, currentTime, nextNote), currentTime);
    }

    return Judgement::None;
}

void Game::onKeyRelease(int lane, int64_t atTime) {
    int64_t currentTime;
    if (atTime != INT64_MIN) {
        currentTime = atTime;
    } else {
        currentTime = getCurrentGameTime();
    }

    addDebugLog(currentTime, "KEY_UP", lane, "");

    for (auto& note : beatmap.notes) {
        if (note.isFakeNote) continue;  // Skip fake notes
        if (note.state != NoteState::Holding || note.lane != lane) continue;

        int64_t rawTailError = std::abs(note.endTime - currentTime);

        // Use raw tail error for judgement calculation
        // (Legacy mode results match osu! replay, so we use the same approach)
        double tailError = rawTailError;

        // Check if release is within tail judgement window
        double tailWindow = judgementSystem.getBadWindow();
        if (rawTailError <= tailWindow) {
            // If head was not hit, release in tail window -> Miss
            if (!note.headHit) {
                note.state = NoteState::Missed;
                note.hadComboBreak = true;
                processJudgement(Judgement::Miss, lane);
                combo = 0;
                return;
            }

            // Process remaining ticks before ending hold note
            int64_t tickEndTime = std::min(currentTime, note.endTime);
            while (note.nextTickTime + 100 <= tickEndTime) {
                note.nextTickTime += 100;
                combo++;
                if (combo > maxCombo) maxCombo = combo;
            }
            // Play tail key sound
            keySoundManager.playKeySound(note, true);

            // Notify storyboard of tail hitsound
            storyboard.onHitSound(buildHitSoundInfo(note, beatmap.timingPoints, currentTime, true), currentTime);

            // Normal release at tail
            note.state = NoteState::Hit;
            renderer.triggerLightingN(lane, currentTime);

            Judgement j;
            double headError = note.headHitError;
            double combinedError = headError + tailError;

            if (settings.judgeMode == JudgementMode::O2Jam) {
                // O2Jam mode: use overlap-based judgement for hold tail
                // Calculate tail overlap using Hi-Speed
                double hiSpeed = settings.scrollSpeed / 3.657 * clockRate;
                int tailNoteY = renderer.getNoteY(note.endTime, currentTime, settings.scrollSpeed, baseBPM,
                                                   settings.bpmScaleMode, beatmap.timingPoints,
                                                   settings.ignoreSV, clockRate);
                int judgeLineY = renderer.getJudgeLineY();
                double tailOverlap = calcOverlapPercent(tailNoteY, judgeLineY, Renderer::NOTE_HEIGHT, hiSpeed);

                // O2Jam only has Cool/Good/Bad/Miss (no Perfect/Good100)
                if (note.hadComboBreak) {
                    // Had combo break - can only get Good(200) or Bad(50)
                    if (tailOverlap >= 0.50) {
                        j = Judgement::Great;  // Good -> 200
                    } else {
                        j = Judgement::Bad;    // Bad -> 50
                    }
                } else {
                    j = judgementSystem.getJudgementByOverlap(tailOverlap);
                }
            } else if (settings.legacyHoldJudgement) {
                // Legacy mode: simple combined error check
                if (note.hadComboBreak) {
                    // Had combo break - can only get Great(200) or Bad(50)
                    if (headError <= judgementSystem.getGreatWindow() && combinedError <= judgementSystem.getGreatWindow() * 2) {
                        j = Judgement::Great;
                    } else {
                        j = Judgement::Bad;
                    }
                } else {
                    // Normal hold - combined judgement (legacy)
                    if (headError <= judgementSystem.getMarvelousWindow() * 1.2 &&
                        combinedError <= judgementSystem.getMarvelousWindow() * 2.4) {
                        j = Judgement::Marvelous;
                    } else if (headError <= judgementSystem.getPerfectWindow() * 1.1 &&
                               combinedError <= judgementSystem.getPerfectWindow() * 2.2) {
                        j = Judgement::Perfect;
                    } else if (headError <= judgementSystem.getGreatWindow() &&
                               combinedError <= judgementSystem.getGreatWindow() * 2) {
                        j = Judgement::Great;
                    } else if (headError <= judgementSystem.getGoodWindow() &&
                               combinedError <= judgementSystem.getGoodWindow() * 2) {
                        j = Judgement::Good;
                    } else {
                        j = Judgement::Bad;
                    }
                }
            } else {
                // osu! mode: use same logic as legacy (since legacy results match osu!)
                if (note.hadComboBreak) {
                    if (headError <= judgementSystem.getGreatWindow() && combinedError <= judgementSystem.getGreatWindow() * 2) {
                        j = Judgement::Great;
                    } else {
                        j = Judgement::Bad;
                    }
                } else {
                    if (headError <= judgementSystem.getMarvelousWindow() * 1.2 &&
                        combinedError <= judgementSystem.getMarvelousWindow() * 2.4) {
                        j = Judgement::Marvelous;
                    } else if (headError <= judgementSystem.getPerfectWindow() * 1.1 &&
                               combinedError <= judgementSystem.getPerfectWindow() * 2.2) {
                        j = Judgement::Perfect;
                    } else if (headError <= judgementSystem.getGreatWindow() &&
                               combinedError <= judgementSystem.getGreatWindow() * 2) {
                        j = Judgement::Great;
                    } else if (headError <= judgementSystem.getGoodWindow() &&
                               combinedError <= judgementSystem.getGoodWindow() * 2) {
                        j = Judgement::Good;
                    } else {
                        j = Judgement::Bad;
                    }
                }
            }

            // Adjust judgement based on enabled state
            j = judgementSystem.adjustForEnabled(j);

            // Call processJudgement to update combo and score
            processJudgement(j, lane);
            hitErrors.push_back({(int64_t)SDL_GetTicks(), currentTime - note.endTime});
        } else if (currentTime < note.endTime - judgementSystem.getBadWindow()) {
            // Released early (mid-hold) - process ticks up to current time first
            if (note.nextTickTime != INT64_MAX) {
                while (note.nextTickTime + 100 <= currentTime) {
                    note.nextTickTime += 100;
                    combo++;
                    if (combo > maxCombo) maxCombo = combo;
                }
            }
            // combo break but can recover
            note.state = NoteState::Released;
            note.hadComboBreak = true;
            // Only set headReleaseTime on first release - don't reset on subsequent releases
            if (note.headReleaseTime == 0) {
                note.headReleaseTime = currentTime;  // Record release time for head falling
                note.headGrayStartTime = currentTime;  // Start gray transition (60ms) - only on first release
            }
            note.nextTickTime = INT64_MAX;  // Disable ticks until recovery
            SDL_Log("HOLD_EARLY_RELEASE: lane=%d headReleaseTime=%lld", lane, (long long)note.headReleaseTime);
            addDebugLog(currentTime, "HOLD_EARLY_RELEASE", lane,
                "noteTime=" + std::to_string(note.time) + " endTime=" + std::to_string(note.endTime) +
                " combo=" + std::to_string(combo) + " maxCombo=" + std::to_string(maxCombo));
            combo = 0;
        } else {
            // Released too late
            note.state = NoteState::Missed;
            // Only record miss if head wasn't already missed
            if (!note.hadComboBreak) {
                processJudgement(Judgement::Miss, note.lane);
            }
        }
        return;
    }
}

Judgement Game::getJudgement(int64_t diff, int64_t noteTime, int64_t currentTime) {
    // For O2Jam mode, use overlap-based judgement
    if (settings.judgeMode == JudgementMode::O2Jam) {
        // Convert scrollSpeed to O2Jam Hi-Speed, scaled by clockRate (DT/NC/HT)
        double hiSpeed = settings.scrollSpeed / 3.657 * clockRate;
        int noteY = renderer.getNoteY(noteTime, currentTime, settings.scrollSpeed, baseBPM,
                                       settings.bpmScaleMode, beatmap.timingPoints,
                                       settings.ignoreSV, clockRate);
        int judgeLineY = renderer.getJudgeLineY();
        double overlap = calcOverlapPercent(noteY, judgeLineY, Renderer::NOTE_HEIGHT, hiSpeed);
        return judgementSystem.getJudgementByOverlap(overlap);
    }

    // For other modes, use time-based judgement
    return judgementSystem.getJudgement(diff);
}

void Game::processJudgement(Judgement j, int lane) {
    static const char* names[] = {"", "Marvelous!!", "Perfect!", "Great", "Good", "Bad", "Miss"};
    int idx = static_cast<int>(j) - 1;
    if (idx >= 0 && idx < 6) {
        judgementCounts[idx]++;
        // Update PP calculator
        ppCalculator.processJudgement(idx);
    }

    lastJudgementText = names[static_cast<int>(j)];
    lastJudgementIndex = idx;  // 0=300g, 1=300, 2=200, 3=100, 4=50, 5=miss
    lastJudgementTime = SDL_GetTicks();

    // Process HP change
    hpManager.processJudgement(j);

    // Trigger BMS Poor BGA on miss
    if (j == Judgement::Miss && isBmsBga) {
        bmsBgaManager.triggerMissLayer(getCurrentGameTime());
    }

    // Sudden Death: any miss triggers death
    // Replay mode ignores Sudden Death mod
    if (settings.suddenDeathEnabled && !replayMode && j == Judgement::Miss && !autoPlay) {
        state = GameState::Dead;
        deathTime = getCurrentGameTime();
        deathSlowdown = 1.0f;
        deathMenuSelection = 1;  // Default to Retry
        if (hasBackgroundMusic) {
            audio.pause();
        }
    }

    // Score calculation (osu!mania formula)
    // HitValue: 320, 300, 200, 100, 50, 0
    // HitBonusValue: 32, 32, 16, 8, 4, 0
    // HitBonus: 2, 1, 0, 0, 0, 0
    // HitPunishment: 0, 0, 8, 24, 44, reset to 0
    static const int hitValues[] = {320, 300, 200, 100, 50, 0};
    static const int hitBonusValues[] = {32, 32, 16, 8, 4, 0};
    static const int hitBonus[] = {2, 1, 0, 0, 0, 0};
    static const int hitPunishment[] = {0, 0, 8, 24, 44, 0};

    if (totalNotes > 0 && idx >= 0 && idx < 6) {
        // Update bonus first (before score calculation)
        if (j == Judgement::Miss) {
            bonus = 0;
        } else {
            bonus = bonus + hitBonus[idx] - hitPunishment[idx];
            if (bonus < 0) bonus = 0;
            if (bonus > 100) bonus = 100;
        }

        // osu!mania: reset bonus to 100 when combo is multiple of 384
        if (combo != 0 && combo % 384 == 0) {
            bonus = 100;
        }

        double baseScore = (500000.0 / totalNotes) * (hitValues[idx] / 320.0);
        double bonusScore = (500000.0 / totalNotes) * (hitBonusValues[idx] * sqrt(bonus) / 320.0);
        scoreAccumulator += (baseScore + bonusScore) * scoreMultiplier;  // Apply score multiplier
        score = static_cast<int>(round(scoreAccumulator));
    }

    bool breaksCombo = (j == Judgement::Miss);
    if (settings.judgeMode == JudgementMode::CustomWindows && idx >= 0 && idx < 6) {
        breaksCombo = settings.judgements[idx].breaksCombo;
    }

    if (!breaksCombo) {
        combo++;
        if (combo > maxCombo) maxCombo = combo;
        // Combo increased - trigger animation
        lastComboChangeTime = SDL_GetTicks();
        comboBreak = false;
    } else {
        // Combo break - trigger break animation
        if (combo >= 2) {
            comboBreak = true;
            comboBreakTime = SDL_GetTicks();
            lastComboValue = combo;
        }
        combo = 0;
    }

    // Debug logging (after combo update)
    int64_t currentTime = getCurrentGameTime();
    addDebugLog(currentTime, "JUDGEMENT", lane,
        std::string(names[static_cast<int>(j)]) + " combo=" + std::to_string(combo) +
        " bonus=" + std::to_string((int)bonus) + " score=" + std::to_string(score));
}

int64_t Game::getCurrentGameTime() const {
    // During pause fade out, return frozen game time
    if (pauseFadingOut) {
        return pauseGameTime;
    }
    int64_t elapsed = SDL_GetTicks() - startTime;
    if (!musicStarted) {
        return static_cast<int64_t>((elapsed - PREPARE_TIME) * clockRate);
    }
    if (hasBackgroundMusic) {
        return audio.getPosition() + settings.audioOffset;
    }
    // No background music: use elapsed time scaled by playback rate
    return static_cast<int64_t>((elapsed - PREPARE_TIME) * clockRate);
}

int64_t Game::getRenderTime() const {
    // During pause fade out, return frozen game time
    if (pauseFadingOut) {
        return pauseGameTime;
    }
    int64_t elapsed = SDL_GetTicks() - startTime;
    if (!musicStarted) {
        return static_cast<int64_t>((elapsed - PREPARE_TIME) * clockRate);
    }
    if (hasBackgroundMusic) {
        return audio.getRawPosition();
    }
    // No background music: use elapsed time scaled by playback rate
    return static_cast<int64_t>((elapsed - PREPARE_TIME) * clockRate);
}

double Game::calculateAccuracy() {
    int total = 0;
    for (int i = 0; i < 6; i++) total += judgementCounts[i];
    if (total == 0) return 0.0;

    double score = 0.0;
    if (settings.judgeMode == JudgementMode::CustomWindows) {
        for (int i = 0; i < 6; i++) {
            score += judgementCounts[i] * settings.judgements[i].accuracy;
        }
        return score / total;
    } else {
        score = 300.0 * (judgementCounts[0] + judgementCounts[1])
              + 200.0 * judgementCounts[2]
              + 100.0 * judgementCounts[3]
              + 50.0 * judgementCounts[4];
        return score / (300.0 * total) * 100.0;
    }
}

void Game::addDebugLog(int64_t time, const std::string& eventType, int lane, const std::string& details) {
    if (!settings.debugEnabled) return;
    debugLog.push_back({time, eventType, lane, details});
}

void Game::exportDebugLog() {
    if (debugLog.empty()) return;

    std::string filename = "debug_" + std::to_string(SDL_GetTicks()) + ".log";
    std::ofstream file(filename);
    if (!file.is_open()) return;

    file << "=== Debug Log ===" << std::endl;
    file << "Beatmap: " << beatmap.title << " [" << beatmap.version << "]" << std::endl;
    file << "Player: " << (replayMode ? replayInfo.playerName : settings.username) << std::endl;
    file << "KeyCount: " << beatmap.keyCount << std::endl;
    file << "TotalNotes: " << totalNotes << std::endl;
    file << "Final Score: " << score << std::endl;
    file << "Final MaxCombo: " << maxCombo << std::endl;
    file << "300g/300/200/100/50/Miss: " << judgementCounts[0] << "/" << judgementCounts[1] << "/"
         << judgementCounts[2] << "/" << judgementCounts[3] << "/" << judgementCounts[4] << "/"
         << judgementCounts[5] << std::endl;
    file << std::endl;
    file << "=== Events ===" << std::endl;

    for (const auto& entry : debugLog) {
        file << "[" << entry.time << "ms] " << entry.eventType;
        if (entry.lane >= 0) file << " Lane" << entry.lane;
        if (!entry.details.empty()) file << " | " << entry.details;
        file << std::endl;
    }

    file.close();
}

void Game::scanSongsFolder() {
    std::string songsPath = "Songs";

    if (!fs::exists(songsPath)) {
        std::cerr << "Songs folder not found" << std::endl;
        return;
    }

    // Collect current folder paths in Songs directory
    std::set<std::string> currentFolders;
    for (const auto& entry : fs::directory_iterator(songsPath)) {
        if (entry.is_directory()) {
            currentFolders.insert(entry.path().string());
        }
    }

    // Build set of existing folder paths in songList
    std::set<std::string> existingFolders;
    for (const auto& song : songList) {
        existingFolders.insert(song.folderPath);
    }

    // Remove songs whose folders no longer exist
    songList.erase(
        std::remove_if(songList.begin(), songList.end(),
            [&currentFolders](const SongEntry& song) {
                return currentFolders.find(song.folderPath) == currentFolders.end();
            }),
        songList.end());

    // Find new folders to scan
    std::vector<std::string> newFolders;
    for (const auto& folder : currentFolders) {
        if (existingFolders.find(folder) == existingFolders.end()) {
            newFolders.push_back(folder);
        }
    }

    // If no new folders, just re-sort and return
    if (newFolders.empty()) {
        std::cerr << "[SCAN] No new folders to scan" << std::endl;
        goto sort_and_return;
    }

    std::cerr << "[SCAN] Found " << newFolders.size() << " new folders to scan" << std::endl;

    for (const auto& folderPath : newFolders) {
        fs::path entry(folderPath);

        // Check if we have a valid cached index
        if (SongIndex::isIndexValid(folderPath)) {
            CachedSong cached;
            if (SongIndex::loadIndex(folderPath, cached)) {
                // Convert cached song to SongEntry
                SongEntry song;
                song.folderPath = cached.folderPath;
                song.folderName = cached.folderName;
                song.title = cached.title;
                song.artist = cached.artist;
                song.backgroundPath = cached.backgroundPath;
                song.audioPath = cached.audioPath;
                song.previewTime = cached.previewTime;
                song.source = static_cast<BeatmapSource>(cached.source);

                std::cerr << "[CACHE] Loading " << cached.title
                          << " cached.difficulties=" << cached.difficulties.size() << std::endl;

                for (const auto& cd : cached.difficulties) {
                    song.beatmapFiles.push_back(cd.path);
                    DifficultyInfo diff;
                    diff.path = cd.path;
                    diff.version = cd.version;
                    diff.creator = cd.creator;
                    diff.hash = cd.hash;
                    diff.backgroundPath = cd.backgroundPath;
                    diff.audioPath = cd.audioPath;
                    diff.previewTime = cd.previewTime;
                    diff.keyCount = cd.keyCount;
                    for (int v = 0; v < STAR_RATING_VERSION_COUNT; v++) {
                        diff.starRatings[v] = cd.starRatings[v];
                    }
                    song.difficulties.push_back(diff);
                }

                std::cerr << "[CACHE] Result: beatmapFiles=" << song.beatmapFiles.size()
                          << " difficulties=" << song.difficulties.size() << std::endl;

                if (!song.beatmapFiles.empty()) {
                    // Sort difficulties by star rating (ascending)
                    std::vector<size_t> indices(song.difficulties.size());
                    std::iota(indices.begin(), indices.end(), 0);
                    std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
                        return song.difficulties[a].starRatings[settings.starRatingVersion] <
                               song.difficulties[b].starRatings[settings.starRatingVersion];
                    });
                    std::vector<std::string> sortedFiles;
                    std::vector<DifficultyInfo> sortedDiffs;
                    for (size_t i : indices) {
                        sortedFiles.push_back(song.beatmapFiles[i]);
                        sortedDiffs.push_back(song.difficulties[i]);
                    }
                    song.beatmapFiles = std::move(sortedFiles);
                    song.difficulties = std::move(sortedDiffs);

                    songList.push_back(song);
                }
                continue;  // Skip to next folder
            }
        }

        // No valid cache, scan the folder
        SongEntry song;
        song.folderPath = entry.string();
        song.folderName = entry.filename().string();

        // Scan for beatmap files (including subdirectories for Malody support)
        for (const auto& file : fs::recursive_directory_iterator(entry)) {
            if (!file.is_regular_file()) continue;
            std::string ext = file.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext == ".osu") {
                song.source = BeatmapSource::Osu;

                // Parse difficulty info from .osu file
                DifficultyInfo diff;
                diff.path = file.path().string();
                diff.keyCount = 4;  // Default
                std::string diffBgFile;
                std::string diffAudioFile;
                std::ifstream diffFile(file.path().string());
                std::string diffLine;
                while (std::getline(diffFile, diffLine)) {
                    if (diffLine.find("Version:") == 0) {
                        diff.version = diffLine.substr(8);
                        while (!diff.version.empty() && diff.version[0] == ' ')
                            diff.version.erase(0, 1);
                    } else if (diffLine.find("Creator:") == 0) {
                        diff.creator = diffLine.substr(8);
                        while (!diff.creator.empty() && diff.creator[0] == ' ')
                            diff.creator.erase(0, 1);
                    } else if (diffLine.find("CircleSize:") == 0) {
                        std::string val = diffLine.substr(11);
                        while (!val.empty() && val[0] == ' ') val.erase(0, 1);
                        try { diff.keyCount = std::stoi(val); } catch (...) {}
                    } else if (diffLine.find("AudioFilename:") == 0) {
                        diffAudioFile = diffLine.substr(14);
                        while (!diffAudioFile.empty() && (diffAudioFile[0] == ' ' || diffAudioFile[0] == '\t'))
                            diffAudioFile.erase(0, 1);
                        while (!diffAudioFile.empty() && (diffAudioFile.back() == '\r' || diffAudioFile.back() == '\n'))
                            diffAudioFile.pop_back();
                    } else if (diffLine.find("PreviewTime:") == 0) {
                        std::string val = diffLine.substr(12);
                        while (!val.empty() && val[0] == ' ') val.erase(0, 1);
                        try { diff.previewTime = std::stoi(val); } catch (...) {}
                    } else if (diffLine.find("[Events]") == 0) {
                        // Found Events section, look for background
                        while (std::getline(diffFile, diffLine)) {
                            if (diffLine.empty() || diffLine[0] == '/' || diffLine[0] == ' ') continue;
                            if (diffLine[0] == '[') break;  // Next section
                            // Background line: 0,0,"filename",0,0
                            if (diffLine.find("0,0,\"") == 0) {
                                size_t start = diffLine.find("\"") + 1;
                                size_t end = diffLine.find("\"", start);
                                if (end != std::string::npos) {
                                    diffBgFile = diffLine.substr(start, end - start);
                                    // Skip video files
                                    std::string bgExt = diffBgFile.substr(diffBgFile.find_last_of(".") + 1);
                                    std::transform(bgExt.begin(), bgExt.end(), bgExt.begin(), ::tolower);
                                    if (bgExt == "mp4" || bgExt == "avi" || bgExt == "flv") {
                                        diffBgFile.clear();
                                    }
                                }
                                break;
                            }
                        }
                        break;  // Stop after Events section
                    } else if (diffLine.find("[Editor]") == 0 || diffLine.find("[Metadata]") == 0) {
                        // Continue to next section
                    } else if (diffLine.find("[Difficulty]") == 0) {
                        // Continue reading
                    }
                }

                // Set per-difficulty background and audio paths
                if (!diffBgFile.empty()) {
                    diff.backgroundPath = song.folderPath + "/" + diffBgFile;
                }
                if (!diffAudioFile.empty()) {
                    diff.audioPath = song.folderPath + "/" + diffAudioFile;
                }

                // Calculate star ratings for all versions
                BeatmapInfo tempInfo;
                if (OsuParser::parse(diff.path, tempInfo)) {
                    diff.starRatings[0] = calculateStarRating(tempInfo.notes, diff.keyCount,
                        StarRatingVersion::OsuStable_b20260101);
                    diff.starRatings[1] = calculateStarRating(tempInfo.notes, diff.keyCount,
                        StarRatingVersion::OsuStable_b20220101);
                }

                // Calculate MD5 hash
                diff.hash = OsuParser::calculateMD5(diff.path);

                // Add both together to keep them in sync
                song.beatmapFiles.push_back(diff.path);
                song.difficulties.push_back(diff);
            } else if (ext == ".bytes") {
                song.source = BeatmapSource::DJMaxRespect;

                // Parse difficulty info from filename
                DifficultyInfo diff;
                diff.path = file.path().string();
                std::string fname = file.path().filename().string();
                std::string lower = fname;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

                // Extract key count
                if (lower.find("_8b_") != std::string::npos) diff.keyCount = 8;
                else if (lower.find("_6b_") != std::string::npos) diff.keyCount = 6;
                else if (lower.find("_5b_") != std::string::npos) diff.keyCount = 5;
                else if (lower.find("_4b_") != std::string::npos) diff.keyCount = 4;
                else diff.keyCount = 4;

                // Extract difficulty name
                if (lower.find("_sc") != std::string::npos) diff.version = "SC";
                else if (lower.find("_mx") != std::string::npos) diff.version = "Maximum";
                else if (lower.find("_hd") != std::string::npos) diff.version = "Hard";
                else if (lower.find("_nm") != std::string::npos) diff.version = "Normal";
                else diff.version = "Normal";

                // Calculate star ratings for all versions
                BeatmapInfo tempInfo;
                if (DJMaxParser::parse(diff.path, tempInfo)) {
                    diff.hash = OsuParser::calculateMD5(diff.path);  // Hash for replay matching
                    diff.starRatings[0] = calculateStarRating(tempInfo.notes, diff.keyCount,
                        StarRatingVersion::OsuStable_b20260101);
                    diff.starRatings[1] = calculateStarRating(tempInfo.notes, diff.keyCount,
                        StarRatingVersion::OsuStable_b20220101);
                }

                // Add both together to keep them in sync
                song.beatmapFiles.push_back(diff.path);
                song.difficulties.push_back(diff);
            } else if (ext == ".ojn") {
                // O2Jam: one file contains 3 difficulties (Easy, Normal, Hard)
                // Get level info from header
                OjnHeader header;
                std::string ojnPath = file.path().string();
                std::string creator;
                if (OjnParser::getHeader(ojnPath, header)) {
                    creator = std::string(header.noter, strnlen(header.noter, 32));

                    // Calculate star ratings for each difficulty (all versions)
                    double starEasy[2] = {0.0, 0.0};
                    double starNormal[2] = {0.0, 0.0};
                    double starHard[2] = {0.0, 0.0};
                    BeatmapInfo tempInfo;
                    if (OjnParser::parse(ojnPath, tempInfo, OjnDifficulty::Easy)) {
                        starEasy[0] = calculateStarRating(tempInfo.notes, 7, StarRatingVersion::OsuStable_b20260101);
                        starEasy[1] = calculateStarRating(tempInfo.notes, 7, StarRatingVersion::OsuStable_b20220101);
                    }
                    if (OjnParser::parse(ojnPath, tempInfo, OjnDifficulty::Normal)) {
                        starNormal[0] = calculateStarRating(tempInfo.notes, 7, StarRatingVersion::OsuStable_b20260101);
                        starNormal[1] = calculateStarRating(tempInfo.notes, 7, StarRatingVersion::OsuStable_b20220101);
                    }
                    if (OjnParser::parse(ojnPath, tempInfo, OjnDifficulty::Hard)) {
                        starHard[0] = calculateStarRating(tempInfo.notes, 7, StarRatingVersion::OsuStable_b20260101);
                        starHard[1] = calculateStarRating(tempInfo.notes, 7, StarRatingVersion::OsuStable_b20220101);
                    }

                    // Build paths
                    std::string pathEasy = ojnPath + ":0:" + std::to_string(header.level[0]);
                    std::string pathNormal = ojnPath + ":1:" + std::to_string(header.level[1]);
                    std::string pathHard = ojnPath + ":2:" + std::to_string(header.level[2]);

                    // Add difficulty info
                    DifficultyInfo diffEasy;
                    diffEasy.path = pathEasy;
                    diffEasy.version = "Easy Lv." + std::to_string(header.level[0]);
                    diffEasy.creator = creator;
                    diffEasy.keyCount = 7;
                    diffEasy.hash = OsuParser::calculateMD5(ojnPath) + ":0";  // Hash for replay matching
                    diffEasy.starRatings[0] = starEasy[0];
                    diffEasy.starRatings[1] = starEasy[1];

                    DifficultyInfo diffNormal;
                    diffNormal.path = pathNormal;
                    diffNormal.version = "Normal Lv." + std::to_string(header.level[1]);
                    diffNormal.creator = creator;
                    diffNormal.keyCount = 7;
                    diffNormal.hash = OsuParser::calculateMD5(ojnPath) + ":1";  // Hash for replay matching
                    diffNormal.starRatings[0] = starNormal[0];
                    diffNormal.starRatings[1] = starNormal[1];

                    DifficultyInfo diffHard;
                    diffHard.path = pathHard;
                    diffHard.version = "Hard Lv." + std::to_string(header.level[2]);
                    diffHard.creator = creator;
                    diffHard.keyCount = 7;
                    diffHard.hash = OsuParser::calculateMD5(ojnPath) + ":2";  // Hash for replay matching
                    diffHard.starRatings[0] = starHard[0];
                    diffHard.starRatings[1] = starHard[1];

                    // Add beatmapFiles and difficulties together
                    song.beatmapFiles.push_back(pathEasy);
                    song.difficulties.push_back(diffEasy);
                    song.beatmapFiles.push_back(pathNormal);
                    song.difficulties.push_back(diffNormal);
                    song.beatmapFiles.push_back(pathHard);
                    song.difficulties.push_back(diffHard);
                } else {
                    std::string ojnHash = OsuParser::calculateMD5(ojnPath);
                    std::string p0 = ojnPath + ":0:0";
                    std::string p1 = ojnPath + ":1:0";
                    std::string p2 = ojnPath + ":2:0";
                    DifficultyInfo d1; d1.path = p0; d1.version = "Easy"; d1.keyCount = 7; d1.hash = ojnHash + ":0";
                    DifficultyInfo d2; d2.path = p1; d2.version = "Normal"; d2.keyCount = 7; d2.hash = ojnHash + ":1";
                    DifficultyInfo d3; d3.path = p2; d3.version = "Hard"; d3.keyCount = 7; d3.hash = ojnHash + ":2";
                    song.beatmapFiles.push_back(p0);
                    song.difficulties.push_back(d1);
                    song.beatmapFiles.push_back(p1);
                    song.difficulties.push_back(d2);
                    song.beatmapFiles.push_back(p2);
                    song.difficulties.push_back(d3);
                }
                song.source = BeatmapSource::O2Jam;
            } else if (ext == ".pt") {
                song.source = BeatmapSource::DJMaxOnline;  // DJMAX Online PT files

                // Parse difficulty info from filename
                DifficultyInfo diff;
                diff.path = file.path().string();
                std::string fname = file.path().filename().string();
                std::string lower = fname;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

                // Detect key count from filename or auto-detect
                if (lower.find("_7k") != std::string::npos || lower.find("7k_") != std::string::npos ||
                    lower.find("_7b") != std::string::npos || lower.find("7b_") != std::string::npos) {
                    diff.keyCount = 7;
                } else if (lower.find("_5k") != std::string::npos || lower.find("5k_") != std::string::npos ||
                           lower.find("_5b") != std::string::npos || lower.find("5b_") != std::string::npos) {
                    diff.keyCount = 5;
                } else {
                    diff.keyCount = 5;  // Default to 5K
                }

                // Extract difficulty name
                // Simplified format: fire_5kez2.pt, fire_5knm5.pt, fire_5kMX.pt
                // No suffix (e.g., fire_5k.pt) = Hard difficulty
                diff.version = "Hard";  // Default

                // First try standard format
                if (lower.find("_hd_") != std::string::npos || lower.find("_hard") != std::string::npos) {
                    diff.version = "Hard";
                } else if (lower.find("_nm_") != std::string::npos || lower.find("_normal") != std::string::npos) {
                    diff.version = "Normal";
                } else if (lower.find("_ez_") != std::string::npos || lower.find("_easy") != std::string::npos) {
                    diff.version = "Easy";
                } else if (lower.find("_mx_") != std::string::npos || lower.find("_max") != std::string::npos) {
                    diff.version = "Maximum";
                } else if (lower.find("_sc_") != std::string::npos || lower.find("_sc.") != std::string::npos) {
                    diff.version = "SC";
                } else {
                    // Try simplified format: extract difficulty after _5k or _7k
                    size_t kpos = lower.find("_5k");
                    if (kpos == std::string::npos) kpos = lower.find("_7k");
                    if (kpos != std::string::npos && kpos + 3 < lower.length()) {
                        std::string suffix = lower.substr(kpos + 3);
                        if (suffix.find("hd") == 0) diff.version = "Hard";
                        else if (suffix.find("mx") == 0) diff.version = "Maximum";
                        else if (suffix.find("sc") == 0) diff.version = "SC";
                        else if (suffix.find("nm") == 0) diff.version = "Normal";
                        else if (suffix.find("ez") == 0) diff.version = "Easy";
                    }
                }

                // Calculate star ratings
                BeatmapInfo tempInfo;
                if (PTParser::parse(diff.path, tempInfo)) {
                    diff.keyCount = tempInfo.keyCount;  // Use detected key count
                    diff.hash = OsuParser::calculateMD5(diff.path);  // Calculate hash for replay matching
                    diff.starRatings[0] = calculateStarRating(tempInfo.notes, diff.keyCount,
                        StarRatingVersion::OsuStable_b20260101);
                    diff.starRatings[1] = calculateStarRating(tempInfo.notes, diff.keyCount,
                        StarRatingVersion::OsuStable_b20220101);
                }

                // Set background image path for DJMAX Online
                // Format: song/{songname}/eyecatch/{songname}_{diff}.jpg or {songname}_ORG_{diff}.jpg
                {
                    // Track extracted PAKs - each PAK only extracted once
                    static std::unordered_map<std::string, bool> extractedPaks;

                    // Extract song name from PT filename (e.g., "baramlive_5kez2.pt" -> "baramlive")
                    std::string stem = file.path().stem().string();
                    size_t underscorePos = stem.find('_');
                    if (underscorePos != std::string::npos) {
                        std::string songName = stem.substr(0, underscorePos);

                        // Find PAK file
                        fs::path pakPath = file.path().parent_path() / (songName + ".pak");
                        std::string pakKey = pakPath.string();
                        // Use Data/DJMaxBG instead of Data/Tmp to persist across restarts
                        fs::path tempDir = fs::current_path() / "Data" / "BG" / pakPath.stem().string();

                        // Extract all eyecatch images from PAK only once
                        if (fs::exists(pakPath) && extractedPaks.find(pakKey) == extractedPaks.end()) {
                            extractedPaks[pakKey] = true;  // Mark as processed
                            SDL_Log("[PAK BG] Processing: %s", pakPath.string().c_str());

                            static PakExtractor bgPakExtractor;
                            static bool bgKeysLoaded = false;
                            if (!bgKeysLoaded) {
                                bgKeysLoaded = bgPakExtractor.loadKeys("D:\\work\\DJMax_Online\\Xip-Pak-Extractor-main\\keyFiles");
                                SDL_Log("[PAK BG] Keys loaded: %d", bgKeysLoaded);
                            }
                            if (bgKeysLoaded && bgPakExtractor.open(pakPath.string())) {
                                SDL_Log("[PAK BG] Opened, files: %zu", bgPakExtractor.getFileList().size());
                                // Print first 5 filenames to see structure
                                int printed = 0;
                                for (const auto& pakFile : bgPakExtractor.getFileList()) {
                                    if (printed < 5) {
                                        SDL_Log("[PAK BG] File: %s", pakFile.filename.c_str());
                                        printed++;
                                    }
                                }
                                int eyecatchCount = 0;
                                for (const auto& pakFile : bgPakExtractor.getFileList()) {
                                    // Case-insensitive check for eyecatch
                                    std::string lowerName = pakFile.filename;
                                    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                                    if (lowerName.find("/eyecatch/") != std::string::npos ||
                                        lowerName.find("\\eyecatch\\") != std::string::npos) {
                                        eyecatchCount++;
                                        std::string ext = fs::path(pakFile.filename).extension().string();
                                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                        if (ext == ".jpg" || ext == ".png" || ext == ".jpeg") {
                                            fs::path outPath = tempDir / pakFile.filename;
                                            if (!fs::exists(outPath)) {
                                                std::vector<uint8_t> data;
                                                if (bgPakExtractor.extractFile(pakFile.filename, data)) {
                                                    fs::create_directories(outPath.parent_path());
                                                    std::ofstream out(outPath, std::ios::binary);
                                                    if (out) {
                                                        out.write(reinterpret_cast<char*>(data.data()), data.size());
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                SDL_Log("[PAK BG] Found %d eyecatch files", eyecatchCount);
                                bgPakExtractor.close();
                            }
                        }

                        // Map difficulty version to suffix
                        std::string diffSuffix = "hd";
                        if (diff.version == "Easy") diffSuffix = "ez";
                        else if (diff.version == "Normal") diffSuffix = "nm";
                        else if (diff.version == "Hard") diffSuffix = "hd";
                        else if (diff.version == "Maximum") diffSuffix = "mx";
                        else if (diff.version == "SC") diffSuffix = "sc";

                        // Check for extracted file
                        fs::path outPath1 = tempDir / "song" / songName / "eyecatch" / (songName + "_" + diffSuffix + ".jpg");
                        fs::path outPath2 = tempDir / "song" / songName / "eyecatch" / (songName + "_ORG_" + diffSuffix + ".jpg");
                        if (fs::exists(outPath1)) {
                            diff.backgroundPath = outPath1.string();
                        } else if (fs::exists(outPath2)) {
                            diff.backgroundPath = outPath2.string();
                        }
                    }
                }

                song.beatmapFiles.push_back(diff.path);
                song.difficulties.push_back(diff);
            } else if (ext == ".bms" || ext == ".bme" || ext == ".bml" || ext == ".pms") {
                song.source = BeatmapSource::BMS;

                DifficultyInfo diff;
                diff.path = file.path().string();
                diff.keyCount = 7;  // Default, will be updated after parsing

                // Parse BMS to get metadata and key count
                BeatmapInfo tempInfo;
                if (BMSParser::parse(diff.path, tempInfo)) {
                    diff.keyCount = tempInfo.keyCount;
                    diff.hash = OsuParser::calculateMD5(diff.path);  // Hash for replay matching
                    // For BMS: version = TITLE + ARTIST + Lv.X
                    std::string diffName = tempInfo.title;
                    if (!tempInfo.artist.empty()) {
                        diffName += " " + tempInfo.artist;
                    }
                    if (!tempInfo.version.empty()) {
                        diffName += " " + tempInfo.version;  // Append "Lv.X"
                    }
                    diff.version = diffName.empty() ? file.path().stem().string() : diffName;
                    diff.creator = tempInfo.creator;  // SUBARTIST as charter
                    diff.starRatings[0] = calculateStarRating(tempInfo.notes, diff.keyCount,
                        StarRatingVersion::OsuStable_b20260101);
                    diff.starRatings[1] = calculateStarRating(tempInfo.notes, diff.keyCount,
                        StarRatingVersion::OsuStable_b20220101);
                } else {
                    diff.version = file.path().stem().string();
                }

                song.beatmapFiles.push_back(diff.path);
                song.difficulties.push_back(diff);
            } else if (ext == ".1") {
                // beatmania IIDX chart file
                song.source = BeatmapSource::IIDX;

                // Extract song ID from filename (e.g., "32083.1" -> 32083)
                std::string stem = file.path().stem().string();
                int songId = 0;
                try {
                    songId = std::stoi(stem);
                } catch (...) {
                    songId = 0;
                }

                // Look up song info from database
                static auto iidxDB = getIIDXSongDB();
                auto it = iidxDB.find(songId);
                if (it != iidxDB.end()) {
                    song.title = it->second.title;
                    song.artist = it->second.artist;
                } else {
                    song.title = stem;
                    song.artist = "Unknown";
                }

                try {
                    std::cout << "[IIDX] Scanning: " << file.path().string() << std::endl;
                    auto availDiffs = IIDXParser::getAvailableDifficulties(file.path().string());
                    std::cout << "[IIDX] Found " << availDiffs.size() << " difficulties" << std::endl;
                    std::string iidxFileHash = OsuParser::calculateMD5(file.path().string());

                    for (int diffIdx : availDiffs) {
                        DifficultyInfo diff;
                        diff.path = file.path().string();
                        diff.keyCount = (diffIdx >= 5) ? 16 : 8;  // DP = 16, SP = 8
                        diff.version = IIDXParser::getDifficultyName(diffIdx);
                        diff.hash = iidxFileHash + ":" + std::to_string(diffIdx);  // Hash with difficulty index

                        std::cout << "[IIDX] Parsing difficulty: " << diff.version << std::endl;

                        // Parse to get note count for star rating
                        BeatmapInfo tempInfo;
                        if (IIDXParser::parse(diff.path, tempInfo, diffIdx)) {
                            std::cout << "[IIDX] Calculating star rating..." << std::endl;
                            diff.starRatings[0] = calculateStarRating(tempInfo.notes, diff.keyCount,
                                StarRatingVersion::OsuStable_b20260101);
                            diff.starRatings[1] = calculateStarRating(tempInfo.notes, diff.keyCount,
                                StarRatingVersion::OsuStable_b20220101);
                            std::cout << "[IIDX] Star rating done" << std::endl;
                        }

                        song.beatmapFiles.push_back(diff.path);
                        song.difficulties.push_back(diff);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[IIDX] Exception: " << e.what() << std::endl;
                    continue;
                } catch (...) {
                    std::cerr << "[IIDX] Unknown exception" << std::endl;
                    continue;
                }
            } else if (ext == ".mc") {
                // Malody chart - only Key mode (mode=0) is supported
                BeatmapInfo tempInfo;
                if (!MalodyParser::parse(file.path().string(), tempInfo)) {
                    // Unsupported mode (catch, ring, etc.) - skip this file
                    continue;
                }

                song.source = BeatmapSource::Malody;

                DifficultyInfo diff;
                diff.path = file.path().string();
                diff.keyCount = tempInfo.keyCount;
                diff.version = tempInfo.version.empty() ? file.path().stem().string() : tempInfo.version;
                diff.creator = tempInfo.creator;
                diff.hash = tempInfo.beatmapHash;
                diff.starRatings[0] = calculateStarRating(tempInfo.notes, diff.keyCount,
                    StarRatingVersion::OsuStable_b20260101);
                diff.starRatings[1] = calculateStarRating(tempInfo.notes, diff.keyCount,
                    StarRatingVersion::OsuStable_b20220101);

                song.beatmapFiles.push_back(diff.path);
                song.difficulties.push_back(diff);
            } else if (ext == ".txt") {
                // MUSYNX chart - check for 4T or 6T in filename
                std::string fname = file.path().filename().string();
                if (fname.find("4T") != std::string::npos || fname.find("6T") != std::string::npos) {
                    song.source = BeatmapSource::MuSynx;

                    DifficultyInfo diff;
                    diff.path = file.path().string();
                    diff.keyCount = MuSynxParser::getKeyCountFromFilename(fname);

                    // Extract difficulty from filename
                    std::string lower = fname;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                    if (lower.find("_easy") != std::string::npos) diff.version = "Easy";
                    else if (lower.find("_hard") != std::string::npos) diff.version = "Hard";
                    else if (lower.find("_in.") != std::string::npos || lower.find("_in4t") != std::string::npos ||
                             lower.find("_in6t") != std::string::npos) diff.version = "Inferno";
                    else diff.version = "Normal";

                    // Parse to get star rating
                    BeatmapInfo tempInfo;
                    if (MuSynxParser::parse(diff.path, tempInfo)) {
                        diff.hash = OsuParser::calculateMD5(diff.path);  // Hash for replay matching
                        diff.starRatings[0] = calculateStarRating(tempInfo.notes, diff.keyCount,
                            StarRatingVersion::OsuStable_b20260101);
                        diff.starRatings[1] = calculateStarRating(tempInfo.notes, diff.keyCount,
                            StarRatingVersion::OsuStable_b20220101);
                    }

                    song.beatmapFiles.push_back(diff.path);
                    song.difficulties.push_back(diff);
                }
            }
        }

        if (song.beatmapFiles.empty()) continue;

        // Read metadata from first beatmap file
        std::string firstFile = song.beatmapFiles[0];
        if (song.source == BeatmapSource::Osu) {
            std::ifstream ifs(firstFile);
            std::string line;
            std::string bgFile;
            std::string audioFile;
            song.previewTime = 0;
            while (std::getline(ifs, line)) {
                if (line.find("Title:") == 0) {
                    song.title = line.substr(6);
                } else if (line.find("Artist:") == 0) {
                    song.artist = line.substr(7);
                } else if (line.find("AudioFilename:") == 0) {
                    audioFile = line.substr(14);
                    // Trim whitespace and \r\n
                    while (!audioFile.empty() && (audioFile[0] == ' ' || audioFile[0] == '\t')) audioFile.erase(0, 1);
                    while (!audioFile.empty() && (audioFile.back() == '\r' || audioFile.back() == '\n')) audioFile.pop_back();
                } else if (line.find("PreviewTime:") == 0) {
                    std::string val = line.substr(12);
                    while (!val.empty() && val[0] == ' ') val.erase(0, 1);
                    song.previewTime = std::stoi(val);
                } else if (line.find("[Events]") == 0) {
                    // Found Events section, look for background
                    while (std::getline(ifs, line)) {
                        if (line.empty() || line[0] == '/' || line[0] == ' ') continue;
                        if (line[0] == '[') break;  // Next section
                        // Background line: 0,0,"filename",0,0
                        if (line.find("0,0,\"") == 0) {
                            size_t start = line.find("\"") + 1;
                            size_t end = line.find("\"", start);
                            if (end != std::string::npos) {
                                bgFile = line.substr(start, end - start);
                                // Skip video files
                                std::string ext = bgFile.substr(bgFile.find_last_of(".") + 1);
                                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                if (ext == "mp4" || ext == "avi" || ext == "flv") {
                                    bgFile.clear();
                                }
                            }
                            break;
                        }
                    }
                }
            }
            if (!bgFile.empty()) {
                song.backgroundPath = song.folderPath + "/" + bgFile;
            }
            if (!audioFile.empty()) {
                song.audioPath = song.folderPath + "/" + audioFile;
            }
        } else if (song.source == BeatmapSource::O2Jam) {
            // Extract actual OJN path (remove :difficulty:level suffix)
            std::string ojnPath = firstFile;
            size_t colonPos = ojnPath.rfind(':');
            if (colonPos != std::string::npos && colonPos > 2) {
                size_t colonPos2 = ojnPath.rfind(':', colonPos - 1);
                if (colonPos2 != std::string::npos && colonPos2 > 2) {
                    ojnPath = ojnPath.substr(0, colonPos2);
                }
            }
            OjnHeader header;
            if (OjnParser::getHeader(ojnPath, header)) {
                song.title = std::string(header.title, strnlen(header.title, 64));
                song.artist = std::string(header.artist, strnlen(header.artist, 32));
            }
            // Extract cover image
            song.backgroundPath = OjnParser::extractCover(ojnPath);
            // O2Jam: generate preview from key sounds
            song.audioPath = OjmParser::generatePreview(ojnPath, 30000);
            song.previewTime = -1;  // 40% position
        } else if (song.source == BeatmapSource::DJMaxRespect || song.source == BeatmapSource::DJMaxOnline) {
            BeatmapInfo info;
            // Check if it's a PT file or DJMAX bytes file
            bool parsed = false;
            if (PTParser::isPTFile(firstFile)) {
                parsed = PTParser::parse(firstFile, info);
            } else {
                parsed = DJMaxParser::parse(firstFile, info);
            }
            if (parsed) {
                song.title = info.title;
                song.artist = info.artist;
            }
            // DJMAX: preview audio is "songname.wav" (without "0-" prefix)
            // Extract song name from filename (e.g., "songname_4b_nm.bytes" -> "songname")
            fs::path chartPath(firstFile);
            std::string chartName = chartPath.stem().string();
            size_t underscorePos = chartName.find('_');
            if (underscorePos != std::string::npos) {
                std::string songName = chartName.substr(0, underscorePos);
                // Look for songname.wav or songname.ogg
                for (const auto& ext : {".wav", ".ogg", ".mp3"}) {
                    fs::path audioPath = entry / (songName + ext);
                    if (fs::exists(audioPath)) {
                        song.audioPath = audioPath.string();
                        break;
                    }
                }
            }
            song.previewTime = -1;  // 40% position
        } else if (song.source == BeatmapSource::BMS) {
            // For BMS, find the most common title and artist across all difficulties
            std::unordered_map<std::string, int> titleCounts;
            std::unordered_map<std::string, int> artistCounts;
            for (const auto& diff : song.difficulties) {
                BeatmapInfo info;
                if (BMSParser::parse(diff.path, info)) {
                    if (!info.title.empty()) titleCounts[info.title]++;
                    if (!info.artist.empty()) artistCounts[info.artist]++;
                }
            }
            // Find most common title
            int maxCount = 0;
            for (const auto& [title, count] : titleCounts) {
                if (count > maxCount) {
                    maxCount = count;
                    song.title = title;
                }
            }
            // Find most common artist
            maxCount = 0;
            for (const auto& [artist, count] : artistCounts) {
                if (count > maxCount) {
                    maxCount = count;
                    song.artist = artist;
                }
            }
            song.previewTime = -1;  // 40% position
        } else if (song.source == BeatmapSource::Malody) {
            BeatmapInfo info;
            if (MalodyParser::parse(firstFile, info)) {
                song.title = info.title;
                song.artist = info.artist;
                // Get background and audio from the same directory as the .mc file
                fs::path mcPath(firstFile);
                fs::path mcDir = mcPath.parent_path();
                // Look for background image
                for (const auto& f : fs::directory_iterator(mcDir)) {
                    if (!f.is_regular_file()) continue;
                    std::string ext = f.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".jpg" || ext == ".png" || ext == ".jpeg") {
                        song.backgroundPath = f.path().string();
                        break;
                    }
                }
                // Audio path from beatmap
                if (!info.audioFilename.empty()) {
                    song.audioPath = (mcDir / info.audioFilename).string();
                }
            }
            song.previewTime = -1;  // 40% position
        } else if (song.source == BeatmapSource::MuSynx) {
            BeatmapInfo info;
            if (MuSynxParser::parse(firstFile, info)) {
                song.title = info.title;
                song.artist = info.artist;
            }
            // Look for audio file in folder
            for (const auto& f : fs::directory_iterator(entry)) {
                if (!f.is_regular_file()) continue;
                std::string ext = f.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".ogg" || ext == ".mp3" || ext == ".wav") {
                    song.audioPath = f.path().string();
                    break;
                }
            }
            // Look for background image
            for (const auto& f : fs::directory_iterator(entry)) {
                if (!f.is_regular_file()) continue;
                std::string ext = f.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".jpg" || ext == ".png" || ext == ".jpeg") {
                    song.backgroundPath = f.path().string();
                    break;
                }
            }
            song.previewTime = -1;  // 40% position
        }

        if (song.title.empty()) {
            song.title = song.folderName;
        }

        // Save to index cache
        CachedSong cached;
        cached.folderPath = song.folderPath;
        cached.folderName = song.folderName;
        cached.title = song.title;
        cached.artist = song.artist;
        cached.backgroundPath = song.backgroundPath;
        cached.audioPath = song.audioPath;
        cached.previewTime = song.previewTime;
        cached.source = static_cast<int>(song.source);
        cached.lastModified = SongIndex::getFolderModTime(song.folderPath);
        for (const auto& d : song.difficulties) {
            CachedDifficulty cd;
            cd.path = d.path;
            cd.version = d.version;
            cd.creator = d.creator;
            cd.hash = d.hash;
            cd.backgroundPath = d.backgroundPath;
            cd.audioPath = d.audioPath;
            cd.keyCount = d.keyCount;
            cd.previewTime = d.previewTime;
            for (int v = 0; v < STAR_RATING_VERSION_COUNT; v++) {
                cd.starRatings[v] = d.starRatings[v];
            }
            cached.difficulties.push_back(cd);
        }
        SongIndex::saveIndex(cached);

        // Debug output
        std::cerr << "Song: " << song.title << " beatmapFiles=" << song.beatmapFiles.size()
                  << " difficulties=" << song.difficulties.size() << std::endl;

        // Sort difficulties by star rating (ascending)
        std::vector<size_t> indices(song.difficulties.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
            return song.difficulties[a].starRatings[settings.starRatingVersion] <
                   song.difficulties[b].starRatings[settings.starRatingVersion];
        });
        std::vector<std::string> sortedFiles;
        std::vector<DifficultyInfo> sortedDiffs;
        for (size_t i : indices) {
            sortedFiles.push_back(song.beatmapFiles[i]);
            sortedDiffs.push_back(song.difficulties[i]);
        }
        song.beatmapFiles = std::move(sortedFiles);
        song.difficulties = std::move(sortedDiffs);

        songList.push_back(song);
    }

sort_and_return:
    // Sort song list by title using ICU locale-aware comparison
    // Create ICU collator for sorting
    UErrorCode status = U_ZERO_ERROR;
    UCollator* collator = ucol_open("", &status);  // Default locale
    if (U_SUCCESS(status) && collator) {
        // Set collator strength for case-insensitive comparison
        ucol_setStrength(collator, UCOL_SECONDARY);
        // Enable numeric collation ("2" before "10")
        ucol_setAttribute(collator, UCOL_NUMERIC_COLLATION, UCOL_ON, &status);

        std::sort(songList.begin(), songList.end(),
            [collator](const SongEntry& a, const SongEntry& b) {
                // Convert UTF-8 to UTF-16 for ICU
                UErrorCode err = U_ZERO_ERROR;
                std::vector<UChar> ua(a.title.length() * 2 + 1);
                std::vector<UChar> ub(b.title.length() * 2 + 1);

                int32_t lenA = 0, lenB = 0;
                u_strFromUTF8(ua.data(), (int32_t)ua.size(), &lenA,
                              a.title.c_str(), -1, &err);
                err = U_ZERO_ERROR;
                u_strFromUTF8(ub.data(), (int32_t)ub.size(), &lenB,
                              b.title.c_str(), -1, &err);

                UCollationResult result = ucol_strcoll(collator,
                    ua.data(), lenA, ub.data(), lenB);
                return result == UCOL_LESS;
            });

        ucol_close(collator);
    } else {
        // Fallback to simple comparison if ICU fails
        std::sort(songList.begin(), songList.end(),
            [](const SongEntry& a, const SongEntry& b) {
                return a.title < b.title;
            });
    }
}

void Game::loadSongBackground(int songIndex, int diffIndex) {
    // Cancel any pending background load
    if (bgLoadThread.joinable()) {
        bgLoadThread.join();
    }

    // Free pending load data if any
    {
        std::lock_guard<std::mutex> lock(bgLoadMutex);
        if (bgLoadData) {
            stbi_image_free(bgLoadData);
            bgLoadData = nullptr;
        }
        bgLoadPending = false;
    }

    // Free previous texture
    if (currentBgTexture) {
        SDL_DestroyTexture(currentBgTexture);
        currentBgTexture = nullptr;
    }

    if (songIndex < 0 || songIndex >= (int)songList.size()) return;

    const SongEntry& song = songList[songIndex];

    // Determine background path: use difficulty-specific if available, otherwise song-level
    std::string bgPath;
    if (diffIndex >= 0 && diffIndex < (int)song.difficulties.size() &&
        !song.difficulties[diffIndex].backgroundPath.empty()) {
        bgPath = song.difficulties[diffIndex].backgroundPath;
    } else {
        bgPath = song.backgroundPath;
    }

    if (bgPath.empty()) return;

    // Start async load
    bgLoadThread = std::thread([this, bgPath]() {
        int width, height, channels;
        unsigned char* data = stbi_load(bgPath.c_str(), &width, &height, &channels, 4);

        // Crop DJMAX Online eyecatch images (1024x1024 -> 800x600)
        if (data && width == 1024 && height == 1024) {
            const int cropW = 800, cropH = 600;
            unsigned char* cropped = (unsigned char*)malloc(cropW * cropH * 4);
            if (cropped) {
                for (int y = 0; y < cropH; y++) {
                    memcpy(cropped + y * cropW * 4, data + y * width * 4, cropW * 4);
                }
                stbi_image_free(data);
                data = cropped;
                width = cropW;
                height = cropH;
            }
        }

        std::lock_guard<std::mutex> lock(bgLoadMutex);
        bgLoadData = data;
        bgLoadWidth = width;
        bgLoadHeight = height;
        bgLoadPending = true;
    });
}

void Game::updateBackgroundLoad() {
    if (!bgLoadPending) return;

    std::lock_guard<std::mutex> lock(bgLoadMutex);
    if (bgLoadData) {
        SDL_Surface* surface = SDL_CreateSurface(bgLoadWidth, bgLoadHeight, SDL_PIXELFORMAT_RGBA32);
        if (surface) {
            SDL_LockSurface(surface);
            memcpy(surface->pixels, bgLoadData, bgLoadWidth * bgLoadHeight * 4);
            SDL_UnlockSurface(surface);
            if (currentBgTexture) {
                SDL_DestroyTexture(currentBgTexture);
            }
            currentBgTexture = SDL_CreateTextureFromSurface(renderer.getRenderer(), surface);
            SDL_DestroySurface(surface);
        }
        stbi_image_free(bgLoadData);
        bgLoadData = nullptr;
    }
    bgLoadPending = false;
}

void Game::playPreviewMusic(int songIndex, int diffIndex) {
    if (songIndex < 0 || songIndex >= (int)songList.size()) return;

    const SongEntry& song = songList[songIndex];

    // Determine audio path and preview time: use difficulty-specific if available
    std::string audioPath;
    int previewTime = 0;
    if (diffIndex >= 0 && diffIndex < (int)song.difficulties.size() &&
        !song.difficulties[diffIndex].audioPath.empty()) {
        audioPath = song.difficulties[diffIndex].audioPath;
        previewTime = song.difficulties[diffIndex].previewTime;
    } else {
        audioPath = song.audioPath;
        previewTime = song.previewTime;
    }

    // Normalize path separators for cross-platform compatibility
    // (index cache may contain Windows-style backslashes or trailing \r\n)
    if (!audioPath.empty()) {
        // Remove trailing \r\n from Windows-created index cache
        while (!audioPath.empty() && (audioPath.back() == '\r' || audioPath.back() == '\n')) {
            audioPath.pop_back();
        }
        // Use fs::path to normalize and get proper UTF-8 encoding
        audioPath = fs::path(audioPath).u8string();
    }

    if (audioPath.empty()) {
        // No audio for this song/difficulty - stop current preview if playing
        if (audio.isPlaying()) {
            stopPreviewMusic();
        }
        currentPreviewAudioPath.clear();
        return;
    }

    // Check if the same audio is already playing - no need to reload
    if (audioPath == currentPreviewAudioPath && audio.isPlaying()) {
        return;
    }

    // Reset playback rate to 1.0 for preview (in case DT/HT was enabled during gameplay)
    audio.setPlaybackRate(1.0f);

    if (audio.isPlaying()) {
        // Fade out current, then play new
        previewFading = true;
        previewFadeIn = false;
        previewFadeStart = SDL_GetTicks();
        previewFadeDuration = 150;
        previewTargetIndex = songIndex;
        previewTargetDiffIndex = diffIndex;
    } else {
        // Start playing directly with fade in
        if (audio.loadMusic(audioPath)) {  // loop by default for preview
            currentPreviewAudioPath = audioPath;
            audio.setVolume(0);
            audio.play();
            // setPosition after play
            if (previewTime > 0) {
                audio.setPosition(previewTime);
            } else if (previewTime == -1) {
                int64_t duration = audio.getDuration();
                audio.setPosition((int64_t)(duration * 0.4));
            }
            previewFading = true;
            previewFadeIn = true;
            previewFadeStart = SDL_GetTicks();
            previewFadeDuration = 200;
            previewTargetIndex = -1;
        }
    }
}

void Game::stopPreviewMusic() {
    if (audio.isPlaying()) {
        previewFading = true;
        previewFadeIn = false;
        previewFadeStart = SDL_GetTicks();
        previewFadeDuration = 150;
        previewTargetIndex = -1;
    }
}

void Game::updatePreviewFade() {
    if (!previewFading) return;

    int64_t elapsed = SDL_GetTicks() - previewFadeStart;
    float t = (float)elapsed / previewFadeDuration;
    if (t > 1.0f) t = 1.0f;

    if (previewFadeIn) {
        // Fade in: 0 -> 100
        audio.setVolume((int)(t * 100));
        if (t >= 1.0f) {
            previewFading = false;
        }
    } else {
        // Fade out: 100 -> 0
        audio.setVolume((int)((1.0f - t) * 100));
        if (t >= 1.0f) {
            audio.stop();
            currentPreviewAudioPath.clear();
            previewFading = false;
            // If there's a target song, play it
            if (previewTargetIndex >= 0) {
                const SongEntry& song = songList[previewTargetIndex];
                // Determine audio path and preview time from difficulty if available
                std::string audioPath;
                int previewTime = 0;
                if (previewTargetDiffIndex >= 0 && previewTargetDiffIndex < (int)song.difficulties.size() &&
                    !song.difficulties[previewTargetDiffIndex].audioPath.empty()) {
                    audioPath = song.difficulties[previewTargetDiffIndex].audioPath;
                    previewTime = song.difficulties[previewTargetDiffIndex].previewTime;
                } else {
                    audioPath = song.audioPath;
                    previewTime = song.previewTime;
                }
                // Normalize path for cross-platform compatibility
                if (!audioPath.empty()) {
                    // Remove trailing \r\n from Windows-created index cache
                    while (!audioPath.empty() && (audioPath.back() == '\r' || audioPath.back() == '\n')) {
                        audioPath.pop_back();
                    }
                    audioPath = fs::path(audioPath).u8string();
                }
                if (!audioPath.empty() && audio.loadMusic(audioPath)) {
                    currentPreviewAudioPath = audioPath;
                    audio.setVolume(0);
                    audio.play();
                    // setPosition after play
                    if (previewTime > 0) {
                        audio.setPosition(previewTime);
                    } else if (previewTime == -1) {
                        int64_t duration = audio.getDuration();
                        audio.setPosition((int64_t)(duration * 0.4));
                    }
                    previewFading = true;
                    previewFadeIn = true;
                    previewFadeStart = SDL_GetTicks();
                    previewFadeDuration = 200;
                    previewTargetIndex = -1;
                }
            }
        }
    }
}
