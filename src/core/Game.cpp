#include "Game.h"
#include "ReplayWriter.h"
#include "BeatmapConverter.h"
#include "DJMaxParser.h"
#include "PTParser.h"
#include "PakExtractor.h"
#include "OjnParser.h"
#include "OjmParser.h"
#include "BMSParser.h"
#include "StarRating.h"
#include "SongIndex.h"
#include "stb_image.h"
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
        if (!diff.creator.empty()) {
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
               previewFadeDuration(200), previewTargetIndex(-1),
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
    TTF_Quit();
    SDL_Quit();
}

bool Game::init() {
#ifdef _WIN32
    // Prevent multiple instances using a named mutex
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "ManiaPlayerSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return false;  // Another instance is already running
    }
#endif

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
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "Beatmap Files (*.osu;*.bytes;*.ojn;*.pt;*.bms;*.bme;*.bml;*.pms)\0*.osu;*.bytes;*.ojn;*.pt;*.bms;*.bme;*.bml;*.pms\0osu! Beatmap (*.osu)\0*.osu\0DJMAX Chart (*.bytes)\0*.bytes\0O2Jam Chart (*.ojn)\0*.ojn\0DJMax Online (*.pt)\0*.pt\0BMS Chart (*.bms;*.bme;*.bml;*.pms)\0*.bms;*.bme;*.bml;*.pms\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) {
        return std::string(filename);
    }
#endif
    return "";
}

std::string Game::openReplayDialog() {
#ifdef _WIN32
    wchar_t filename[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = L"osu! Replay (*.osr)\0*.osr\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        // Convert wchar_t to UTF-8
        int size = WideCharToMultiByte(CP_UTF8, 0, filename, -1, nullptr, 0, nullptr, nullptr);
        std::string result(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, filename, -1, &result[0], size, nullptr, nullptr);
        return result;
    }
#endif
    return "";
}

std::string Game::saveReplayDialog() {
#ifdef _WIN32
    wchar_t filename[MAX_PATH] = L"replay.osr";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = L"osu! Replay (*.osr)\0*.osr\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"osr";
    if (GetSaveFileNameW(&ofn)) {
        // Convert wchar_t to UTF-8
        int size = WideCharToMultiByte(CP_UTF8, 0, filename, -1, nullptr, 0, nullptr, nullptr);
        std::string result(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, filename, -1, &result[0], size, nullptr, nullptr);
        return result;
    }
#endif
    return "";
}

std::string Game::openSkinFolderDialog() {
#ifdef _WIN32
    BROWSEINFOA bi = {};
    bi.lpszTitle = "Select Skin Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        char path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, path)) {
            CoTaskMemFree(pidl);
            return std::string(path);
        }
        CoTaskMemFree(pidl);
    }
#endif
    return "";
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
    beatmap.notes.clear();
    beatmap.timingPoints.clear();
    beatmap.storyboardSamples.clear();
    beatmap.audioFilename.clear();  // Clear audio filename
    for (int i = 0; i < 6; i++) judgementCounts[i] = 0;
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
    debugLog.clear();
    hpManager.reset();
    keySoundManager.clear();
    audio.clearSamples();
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

bool Game::loadBeatmap(const std::string& path) {
    resetGame();
    beatmapPath = path;
    beatmap = BeatmapInfo();  // Clear previous beatmap data

    // Stop any playing audio first (preview music)
    audio.stop();

    // Calculate clockRate early so audio loading uses correct playback rate
    clockRate = 1.0;
    scoreMultiplier = 1.0;
    // Apply DT/NC/HT only if not in replay mode (replay mode uses mods from replay file)
    if (!replayMode) {
        if (settings.halfTimeEnabled) {
            clockRate = 0.75;
            scoreMultiplier = 0.5;
        } else if (settings.doubleTimeEnabled || settings.nightcoreEnabled) {
            clockRate = 1.5;
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
    std::string ptAudioDir;  // For PT files with PAK extraction

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

    std::filesystem::path fullAudioPath = osuPath.parent_path() / beatmap.audioFilename;
    std::string audioPath = fullAudioPath.u8string();

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

        // For O2Jam: set background from extracted cover image
        if (isOjn && !storyboard.hasBackground()) {
            std::string coverPath = OjnParser::extractCover(actualPath);
            if (!coverPath.empty()) {
                storyboard.setBackgroundImage(coverPath);
            }
        }

        storyboard.loadTextures(renderer.getRenderer());
    }

    float od = beatmap.od;
    if (settings.judgeMode == JudgementMode::CustomOD) {
        od = settings.customOD;
    }

    if (settings.judgeMode == JudgementMode::CustomWindows) {
        judgeMarvelous = settings.judgements[0].window;
        judgePerfect = settings.judgements[1].window;
        judgeGreat = settings.judgements[2].window;
        judgeGood = settings.judgements[3].window;
        judgeBad = settings.judgements[4].window;
        judgeMiss = settings.judgements[5].window;
    } else {
        judgeMarvelous = 16.0;
        judgePerfect = 64.0 - 3.0 * od;
        judgeGreat = 97.0 - 3.0 * od;
        judgeGood = 127.0 - 3.0 * od;
        judgeBad = 151.0 - 3.0 * od;
        judgeMiss = 188.0 - 3.0 * od;
    }

    // Scale judgement windows by clockRate (DT/HT compensation)
    // In osu!, game-time windows are scaled by clockRate
    judgeMarvelous = judgeMarvelous * clockRate;
    judgePerfect = judgePerfect * clockRate;
    judgeGreat = judgeGreat * clockRate;
    judgeGood = judgeGood * clockRate;
    judgeBad = judgeBad * clockRate;
    judgeMiss = judgeMiss * clockRate;

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

void Game::cleanupTempDir() {
    // Clean up BGA textures
    for (auto& [name, tex] : bgaTextures) {
        if (tex) SDL_DestroyTexture(tex);
    }
    bgaTextures.clear();
    hasBga = false;

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
                // Append typed character to username (limit to 32 chars)
                if (settings.username.length() < 32) {
                    settings.username += e.text.text;
                }
            }
            if (state == GameState::Settings && editingScrollSpeed) {
                // Append typed character to scroll speed (only digits, limit to 4 chars)
                char c = e.text.text[0];
                if (c >= '0' && c <= '9' && scrollSpeedInput.length() < 4) {
                    scrollSpeedInput += c;
                }
            }
        }
        else if (e.type == SDL_EVENT_KEY_DOWN) {
            if (state == GameState::Menu) {
                if (e.key.key == SDLK_ESCAPE) {
                    running = false;
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
                        songSelectNeedAutoScroll = true;
                    } else if (selectedSongIndex > 0) {
                        // Move to previous song
                        selectedSongIndex--;
                        selectedDifficultyIndex = 0;
                        loadSongBackground(selectedSongIndex);
                        playPreviewMusic(selectedSongIndex);
                        songSelectNeedAutoScroll = true;
                    }
                }
                else if (e.key.key == SDLK_DOWN) {
                    int numDifficulties = (int)songList[selectedSongIndex].beatmapFiles.size();
                    if (selectedDifficultyIndex < numDifficulties - 1) {
                        // Move down within difficulty list
                        selectedDifficultyIndex++;
                        songSelectNeedAutoScroll = true;
                    } else if (selectedSongIndex < (int)songList.size() - 1) {
                        // Move to next song
                        selectedSongIndex++;
                        selectedDifficultyIndex = 0;
                        loadSongBackground(selectedSongIndex);
                        playPreviewMusic(selectedSongIndex);
                        songSelectNeedAutoScroll = true;
                    }
                }
                else if (e.key.key == SDLK_RETURN) {
                    if (!songList.empty() && !songSelectTransition) {
                        std::string path = songList[selectedSongIndex].beatmapFiles[selectedDifficultyIndex];
                        if (loadBeatmap(path)) {
                            stopPreviewMusic();
                            songSelectTransition = true;
                            songSelectTransitionStart = SDL_GetTicks();
                        }
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
                        loadSongBackground(0);
                        playPreviewMusic(0);
                    }
                }
            }
            else if (state == GameState::Settings) {
                if (editingUsername) {
                    // Handle text input for username
                    if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_RETURN) {
                        editingUsername = false;
                    } else if (e.key.key == SDLK_BACKSPACE) {
                        if (!settings.username.empty()) {
                            settings.username.pop_back();
                        }
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
                            // Don't adjust startTime yet - wait for fade out to complete
                            pauseFadingOut = true;
                            pauseFadeOutStart = SDL_GetTicks();
                            state = GameState::Playing;
                        } else if (pauseMenuSelection == 1) {
                            // Retry
                            audio.stop();
                            if (loadBeatmap(beatmapPath)) {
                                state = GameState::Playing;
                                musicStarted = false;
                                startTime = SDL_GetTicks();
                            }
                        } else {
                            // Exit to song select
                            audio.stop();
                            audio.stopAllSamples();
                            state = GameState::SongSelect;
                            renderer.setWindowTitle("Mania Player");
                            if (!songList.empty()) {
                                loadSongBackground(selectedSongIndex);
                                playPreviewMusic(selectedSongIndex);
                            }
                        }
                        break;
                    case SDLK_ESCAPE: {
                        // ESC also resumes with fade out
                        // Don't adjust startTime yet - wait for fade out to complete
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
                                if (autoPlay) mods |= 2048;
                                if (settings.suddenDeathEnabled) mods |= 32;
                                ReplayWriter::write(savePath, beatmap.beatmapHash, exportPlayerName, beatmap.keyCount,
                                                   judgementCounts, maxCombo, score, mods, recordedFrames);
                            }
                        } else if (deathMenuSelection == 1) {
                            // Retry
                            audio.stop();
                            if (loadBeatmap(beatmapPath)) {
                                state = GameState::Playing;
                                musicStarted = false;
                                startTime = SDL_GetTicks();
                            }
                        } else {
                            // Quit to song select
                            audio.stop();
                            audio.stopAllSamples();
                            state = GameState::SongSelect;
                            renderer.setWindowTitle("Mania Player");
                            if (!songList.empty()) {
                                loadSongBackground(selectedSongIndex);
                                playPreviewMusic(selectedSongIndex);
                            }
                        }
                        break;
                }
            }
            else if (state == GameState::Playing && !e.key.repeat) {
                switch (e.key.key) {
                    case SDLK_ESCAPE:
                        pauseGameTime = getCurrentGameTime();  // Save game time before pausing
                        audio.pause();
                        audio.pauseAllSamples();
                        pauseTime = SDL_GetTicks();
                        pauseMenuSelection = 0;
                        state = GameState::Paused;
                        break;
                    case SDLK_SPACE:
                        // Skip functionality
                        if (canSkip) {
                            int64_t currentTime = getCurrentGameTime();
                            if (currentTime < skipTargetTime) {
                                // Start music first if not started
                                if (!musicStarted) {
                                    if (hasBackgroundMusic) {
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

    if (!musicStarted && elapsed >= PREPARE_TIME) {
        if (hasBackgroundMusic) {
            audio.setVolume(settings.volume);  // Restore volume after preview
            audio.play();
        }
        musicStarted = true;
    }

    // For keysound-only maps, use system time instead of audio position
    int64_t currentTime;
    if (!musicStarted) {
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
                    processJudgement(Judgement::Marvelous, note.lane);
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
                processJudgement(Judgement::Marvelous, note.lane);
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
        if (note.state == NoteState::Waiting && note.time < currentTime - judgeBad) {
            if (note.isHold) {
                // Hold note head missed
                note.state = NoteState::Holding;
                note.headHitError = judgeBad;
                // If user is already holding the key, don't gray out, don't allow ticks
                // User must release and re-press to get combo
                if (laneKeyDown[note.lane]) {
                    // User was holding before note arrived - no gray, no combo
                    note.hadComboBreak = false;
                    note.nextTickTime = INT64_MAX;  // Disable ticks
                } else {
                    // Normal head miss
                    note.hadComboBreak = true;
                    note.nextTickTime = currentTime;
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
            if (note.endTime < currentTime - judgeBad) {
                note.state = NoteState::Missed;
                // Record miss when tail times out (whole hold note counts as 1 miss)
                processJudgement(Judgement::Miss, note.lane);
            }
        }
        // Released hold notes - no ticks, but check for timeout
        if (note.state == NoteState::Released && note.isHold) {
            if (note.endTime < currentTime - judgeBad) {
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
                songSelectScroll = 0.0f;
                if (!songList.empty()) {
                    loadSongBackground(0);
                    playPreviewMusic(0);
                }
                state = GameState::SongSelect;
            }
        } else {
            // Render disabled button (no click handling)
            renderer.renderButton("Select Beatmap", btnX, btnY, btnW, btnH, -1, -1, false);
            // Show warning message
            renderer.renderText("No Available Beatmap in Songs Folder.", btnX - 60, btnY + btnH + 60);
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

                if (!beatmapPath.empty() && loadBeatmap(beatmapPath)) {
                    replayMode = true;
                    // Replay mode: only enable AutoPlay if replay has AutoPlay mod (2048)
                    autoPlay = (replayInfo.mods & 2048) != 0;

                    // Read DT/NC/HT mods from replay
                    settings.halfTimeEnabled = (replayInfo.mods & 256) != 0;      // HT = 0x100
                    settings.doubleTimeEnabled = (replayInfo.mods & 64) != 0;     // DT = 0x40
                    settings.nightcoreEnabled = (replayInfo.mods & 512) != 0;     // NC = 0x200
                    if (settings.nightcoreEnabled) settings.doubleTimeEnabled = true;

                    // Apply clock rate based on mods
                    if (settings.halfTimeEnabled) {
                        clockRate = 0.75;
                    } else if (settings.doubleTimeEnabled) {
                        clockRate = 1.5;
                    } else {
                        clockRate = 1.0;
                    }
                    audio.setPlaybackRate(clockRate);
                    audio.setChangePitch(settings.nightcoreEnabled);

                    // Recalculate star rating with correct clockRate
                    currentStarRating = calculateStarRating(beatmap.notes, beatmap.keyCount,
                        static_cast<StarRatingVersion>(settings.starRatingVersion), clockRate);

                    // Reinitialize PP calculator with correct star rating
                    ppCalculator.init(totalNotes, currentStarRating);

                    // Rescale judgement windows by clockRate
                    judgeMarvelous = judgeMarvelous * clockRate;
                    judgePerfect = judgePerfect * clockRate;
                    judgeGreat = judgeGreat * clockRate;
                    judgeGood = judgeGood * clockRate;
                    judgeBad = judgeBad * clockRate;
                    judgeMiss = judgeMiss * clockRate;

                    currentReplayFrame = 0;
                    state = GameState::Playing;
                    musicStarted = false;
                    startTime = SDL_GetTicks();
                    // Set window title for replay mode
                    std::string title = "[REPLAY MODE] Mania Player - " + beatmap.artist + " " + beatmap.title +
                                       " [" + beatmap.version + "](" + beatmap.creator + ") Player:" + replayInfo.playerName;
                    renderer.setWindowTitle(title);
                }
            }
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
                autoPlay = settings.autoPlayEnabled;
                state = GameState::Playing;
                musicStarted = false;
                startTime = SDL_GetTicks();
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
                            if (loadBeatmap(path)) {
                                stopPreviewMusic();
                                songSelectTransition = true;
                                songSelectTransitionStart = SDL_GetTicks();
                            }
                        } else {
                            selectedSongIndex = i;
                            selectedDifficultyIndex = 0;
                            loadSongBackground(i);
                            playPreviewMusic(i);
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

                // Source label on the right
                const char* sourceLabel = "";
                if (song.source == BeatmapSource::Osu) sourceLabel = "osu!";
                else if (song.source == BeatmapSource::DJMax) sourceLabel = "DJMAX";
                else if (song.source == BeatmapSource::O2Jam) sourceLabel = "O2Jam";
                else if (song.source == BeatmapSource::BMS) sourceLabel = "BMS";
                renderer.renderText(sourceLabel, 1280 - 80, y + 10);
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
                                    if (loadBeatmap(path)) {
                                        stopPreviewMusic();
                                        songSelectTransition = true;
                                        songSelectTransitionStart = SDL_GetTicks();
                                    }
                                } else {
                                    selectedDifficultyIndex = d;
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
                // Determine mods (AutoPlay = 2048, SuddenDeath = 32)
                int mods = 0;
                if (autoPlay) mods |= 2048;
                if (settings.suddenDeathEnabled) mods |= 32;
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
            renderer.renderTextInput(nullptr, scrollSpeedInput, contentX + 210, row4Y - 5, 50, mouseX, mouseY, mouseClicked, editingScrollSpeed);

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
            const char* judgeModes[] = {"Beatmap OD", "Custom OD", "Custom Windows"};
            int modeIdx = static_cast<int>(settings.judgeMode);
            int newMode = renderer.renderDropdown("Judge Mode", judgeModes, 3, modeIdx, contentX, scrolledY, 200, mouseX, mouseY, mouseClicked, judgeModeDropdownExpanded);
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
            renderer.renderTextInput("Username", settings.username, contentX, usernameY, 200,
                                     mouseX, mouseY, mouseClicked, editingUsername);

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
        renderer.renderKeys(laneKeyDown, settings.selectedKeyCount);
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
        storyboard.renderBackground(renderer.getRenderer());

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

        storyboard.render(renderer.getRenderer(), StoryboardLayer::Background, isPassing);

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
                laneHoldActive[note.lane] = true;
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
            renderer.renderKeys(laneKeyDown, beatmap.keyCount);
            renderer.renderNotes(beatmap.notes, currentTime, settings.scrollSpeed, baseBPM, settings.bpmScaleMode, beatmap.timingPoints, settings.laneColors, useHidden, useFadeIn, combo, settings.ignoreSV, clockRate);
        } else {
            // Default keyImage: Notes below keys
            renderer.renderNotes(beatmap.notes, currentTime, settings.scrollSpeed, baseBPM, settings.bpmScaleMode, beatmap.timingPoints, settings.laneColors, useHidden, useFadeIn, combo, settings.ignoreSV, clockRate);
            renderer.renderKeys(laneKeyDown, beatmap.keyCount);
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
        renderer.renderHitErrorBar(hitErrors, SDL_GetTicks(), judgeMarvelous, judgePerfect, judgeGreat, judgeGood, judgeBad, judgeMiss, settings.hitErrorBarScale);
        renderer.renderFPS(fps);
        renderer.renderGameInfo(currentTime, totalTime, judgementCounts, calculateAccuracy(), score);

        // Performance monitoring (debug mode only)
        if (settings.debugEnabled) {
            char perfText[128];

            // Judgement windows display
            snprintf(perfText, sizeof(perfText), "Windows: %.1f/%.1f/%.1f/%.1f/%.1f/%.1f",
                judgeMarvelous, judgePerfect, judgeGreat, judgeGood, judgeBad, judgeMiss);
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
                // Add total paused duration (including fade out) to startTime
                int64_t totalPausedDuration = SDL_GetTicks() - pauseTime;
                startTime += totalPausedDuration;
                // Calculate offset to correct audio position drift
                if (hasBackgroundMusic) {
                    pauseAudioOffset = pauseGameTime - (audio.getPosition() + settings.audioOffset);
                }
                audio.resume();
                audio.resumeAllSamples();
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

    // Then check for new notes
    for (size_t noteIdx = 0; noteIdx < beatmap.notes.size(); noteIdx++) {
        Note& note = beatmap.notes[noteIdx];
        if (note.state != NoteState::Waiting || note.lane != lane) continue;
        if (note.isFakeNote) continue;  // Skip fake notes - visual only

        int64_t diff = std::abs(note.time - currentTime);

        if (settings.noteLock) {
            // NoteLock: only hit the earliest note in this lane
            if (diff <= judgeBad) {
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
                    renderer.triggerLightingN(lane, currentTime);
                    Judgement j = getJudgement(diff);
                    processJudgement(j, lane);
                }
                // Update next note index for this lane
                updateLaneNextNoteIndex(laneNextNoteIndex, beatmap.notes, lane, static_cast<int>(noteIdx));
                hitErrors.push_back({(int64_t)SDL_GetTicks(), currentTime - note.time});
                return note.isHold ? Judgement::None : getJudgement(diff);
            }
            // In miss window but outside 50 window -> miss
            if (diff <= judgeMiss) {
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
            if (diff <= judgeBad) {
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
                    renderer.triggerLightingN(lane, currentTime);
                    Judgement j = getJudgement(diff);
                    processJudgement(j, lane);
                }
                // Update next note index for this lane
                updateLaneNextNoteIndex(laneNextNoteIndex, beatmap.notes, lane, static_cast<int>(noteIdx));
                hitErrors.push_back({(int64_t)SDL_GetTicks(), currentTime - note.time});
                return note.isHold ? Judgement::None : getJudgement(diff);
            }
            // In miss window but outside 50 window -> miss
            if (diff <= judgeMiss) {
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
        double tailWindow = judgeBad;
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

            if (settings.legacyHoldJudgement) {
                // Legacy mode: simple combined error check
                if (note.hadComboBreak) {
                    // Had combo break - can only get Great(200) or Bad(50)
                    if (headError <= judgeGreat && combinedError <= judgeGreat * 2) {
                        j = Judgement::Great;
                    } else {
                        j = Judgement::Bad;
                    }
                } else {
                    // Normal hold - combined judgement (legacy)
                    if (headError <= judgeMarvelous * 1.2 &&
                        combinedError <= judgeMarvelous * 2.4) {
                        j = Judgement::Marvelous;
                    } else if (headError <= judgePerfect * 1.1 &&
                               combinedError <= judgePerfect * 2.2) {
                        j = Judgement::Perfect;
                    } else if (headError <= judgeGreat &&
                               combinedError <= judgeGreat * 2) {
                        j = Judgement::Great;
                    } else if (headError <= judgeGood &&
                               combinedError <= judgeGood * 2) {
                        j = Judgement::Good;
                    } else {
                        j = Judgement::Bad;
                    }
                }
            } else {
                // osu! mode: use same logic as legacy (since legacy results match osu!)
                if (note.hadComboBreak) {
                    if (headError <= judgeGreat && combinedError <= judgeGreat * 2) {
                        j = Judgement::Great;
                    } else {
                        j = Judgement::Bad;
                    }
                } else {
                    if (headError <= judgeMarvelous * 1.2 &&
                        combinedError <= judgeMarvelous * 2.4) {
                        j = Judgement::Marvelous;
                    } else if (headError <= judgePerfect * 1.1 &&
                               combinedError <= judgePerfect * 2.2) {
                        j = Judgement::Perfect;
                    } else if (headError <= judgeGreat &&
                               combinedError <= judgeGreat * 2) {
                        j = Judgement::Great;
                    } else if (headError <= judgeGood &&
                               combinedError <= judgeGood * 2) {
                        j = Judgement::Good;
                    } else {
                        j = Judgement::Bad;
                    }
                }
            }

            // Call processJudgement to update combo and score
            processJudgement(j, lane);
            hitErrors.push_back({(int64_t)SDL_GetTicks(), currentTime - note.endTime});
        } else if (currentTime < note.endTime - judgeBad) {
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

Judgement Game::getJudgement(int64_t diff) {
    if (diff <= judgeMarvelous) return Judgement::Marvelous;
    if (diff <= judgePerfect) return Judgement::Perfect;
    if (diff <= judgeGreat) return Judgement::Great;
    if (diff <= judgeGood) return Judgement::Good;
    return Judgement::Bad;
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

        // Scan for beatmap files
        for (const auto& file : fs::directory_iterator(entry)) {
            if (!file.is_regular_file()) continue;
            std::string ext = file.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext == ".osu") {
                song.source = BeatmapSource::Osu;

                // Parse difficulty info from .osu file
                DifficultyInfo diff;
                diff.path = file.path().string();
                diff.keyCount = 4;  // Default
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
                    } else if (diffLine.find("[Editor]") == 0 || diffLine.find("[Metadata]") == 0) {
                        // Continue to next section
                    } else if (diffLine.find("[Difficulty]") == 0) {
                        // Continue reading
                    } else if (diffLine.find("[Events]") == 0) {
                        break;  // Stop at Events section
                    }
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
                song.source = BeatmapSource::DJMax;

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
                    diffEasy.starRatings[0] = starEasy[0];
                    diffEasy.starRatings[1] = starEasy[1];

                    DifficultyInfo diffNormal;
                    diffNormal.path = pathNormal;
                    diffNormal.version = "Normal Lv." + std::to_string(header.level[1]);
                    diffNormal.creator = creator;
                    diffNormal.keyCount = 7;
                    diffNormal.starRatings[0] = starNormal[0];
                    diffNormal.starRatings[1] = starNormal[1];

                    DifficultyInfo diffHard;
                    diffHard.path = pathHard;
                    diffHard.version = "Hard Lv." + std::to_string(header.level[2]);
                    diffHard.creator = creator;
                    diffHard.keyCount = 7;
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
                    std::string p0 = ojnPath + ":0:0";
                    std::string p1 = ojnPath + ":1:0";
                    std::string p2 = ojnPath + ":2:0";
                    DifficultyInfo d1; d1.path = p0; d1.version = "Easy"; d1.keyCount = 7;
                    DifficultyInfo d2; d2.path = p1; d2.version = "Normal"; d2.keyCount = 7;
                    DifficultyInfo d3; d3.path = p2; d3.version = "Hard"; d3.keyCount = 7;
                    song.beatmapFiles.push_back(p0);
                    song.difficulties.push_back(d1);
                    song.beatmapFiles.push_back(p1);
                    song.difficulties.push_back(d2);
                    song.beatmapFiles.push_back(p2);
                    song.difficulties.push_back(d3);
                }
                song.source = BeatmapSource::O2Jam;
            } else if (ext == ".pt") {
                song.source = BeatmapSource::DJMax;  // Use DJMax source for PT files

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
                    diff.starRatings[0] = calculateStarRating(tempInfo.notes, diff.keyCount,
                        StarRatingVersion::OsuStable_b20260101);
                    diff.starRatings[1] = calculateStarRating(tempInfo.notes, diff.keyCount,
                        StarRatingVersion::OsuStable_b20220101);
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
                    diff.version = tempInfo.version.empty() ? file.path().stem().string() : tempInfo.version;
                    diff.starRatings[0] = calculateStarRating(tempInfo.notes, diff.keyCount,
                        StarRatingVersion::OsuStable_b20260101);
                    diff.starRatings[1] = calculateStarRating(tempInfo.notes, diff.keyCount,
                        StarRatingVersion::OsuStable_b20220101);
                } else {
                    diff.version = file.path().stem().string();
                }

                song.beatmapFiles.push_back(diff.path);
                song.difficulties.push_back(diff);
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
                    // Trim whitespace
                    while (!audioFile.empty() && audioFile[0] == ' ') audioFile.erase(0, 1);
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
        } else if (song.source == BeatmapSource::DJMax) {
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
            BeatmapInfo info;
            if (BMSParser::parse(firstFile, info)) {
                song.title = info.title;
                song.artist = info.artist;
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
            cd.keyCount = d.keyCount;
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

void Game::loadSongBackground(int index) {
    // Free previous texture
    if (currentBgTexture) {
        SDL_DestroyTexture(currentBgTexture);
        currentBgTexture = nullptr;
    }

    if (index < 0 || index >= (int)songList.size()) return;

    const SongEntry& song = songList[index];
    if (song.backgroundPath.empty()) return;

    int width, height, channels;
    unsigned char* data = stbi_load(song.backgroundPath.c_str(), &width, &height, &channels, 4);
    if (data) {
        SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        if (surface) {
            SDL_LockSurface(surface);
            memcpy(surface->pixels, data, width * height * 4);
            SDL_UnlockSurface(surface);
            currentBgTexture = SDL_CreateTextureFromSurface(renderer.getRenderer(), surface);
            SDL_DestroySurface(surface);
        }
        stbi_image_free(data);
    }
}

void Game::playPreviewMusic(int index) {
    if (index < 0 || index >= (int)songList.size()) return;

    // Reset playback rate to 1.0 for preview (in case DT/HT was enabled during gameplay)
    audio.setPlaybackRate(1.0f);

    if (audio.isPlaying()) {
        // Fade out current, then play new
        previewFading = true;
        previewFadeIn = false;
        previewFadeStart = SDL_GetTicks();
        previewFadeDuration = 150;
        previewTargetIndex = index;
    } else {
        // Start playing directly with fade in
        const SongEntry& song = songList[index];
        if (song.audioPath.empty()) return;

        if (audio.loadMusic(song.audioPath)) {  // loop by default for preview
            audio.setVolume(0);
            audio.play();
            // setPosition after play
            if (song.previewTime > 0) {
                audio.setPosition(song.previewTime);
            } else if (song.previewTime == -1) {
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
            previewFading = false;
            // If there's a target song, play it
            if (previewTargetIndex >= 0) {
                const SongEntry& song = songList[previewTargetIndex];
                if (!song.audioPath.empty() && audio.loadMusic(song.audioPath)) {
                    audio.setVolume(0);
                    audio.play();
                    // setPosition after play
                    if (song.previewTime > 0) {
                        audio.setPosition(song.previewTime);
                    } else if (song.previewTime == -1) {
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
