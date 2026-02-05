#include "SkinManager.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <regex>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace fs = std::filesystem;

// Helper functions
std::string SkinManager::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> SkinManager::split(const std::string& str, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delim)) {
        result.push_back(trim(item));
    }
    return result;
}

SDL_Color SkinManager::parseColor(const std::string& str) {
    auto parts = split(str, ',');
    SDL_Color c = {255, 255, 255, 255};
    if (parts.size() >= 3) {
        c.r = (Uint8)std::stoi(parts[0]);
        c.g = (Uint8)std::stoi(parts[1]);
        c.b = (Uint8)std::stoi(parts[2]);
        c.a = parts.size() >= 4 ? (Uint8)std::stoi(parts[3]) : 255;
    }
    return c;
}

// Constructor and destructor
SkinManager::SkinManager() {}

SkinManager::~SkinManager() {
    unloadSkin();
}

void SkinManager::unloadSkin() {
    for (auto& pair : textureCache) {
        if (pair.second) {
            SDL_DestroyTexture(pair.second);
        }
    }
    textureCache.clear();
    config = SkinConfig();
    loaded = false;
}

void SkinManager::setBeatmapPath(const std::string& path) {
    if (beatmapPath != path) {
        // Clear texture cache when beatmap path changes
        // This ensures beatmap-specific skins are reloaded
        for (auto& pair : textureCache) {
            if (pair.second) {
                SDL_DestroyTexture(pair.second);
            }
        }
        textureCache.clear();
        beatmapPath = path;
    }
}

void SkinManager::clearBeatmapPath() {
    if (!beatmapPath.empty()) {
        // Clear texture cache when clearing beatmap path
        for (auto& pair : textureCache) {
            if (pair.second) {
                SDL_DestroyTexture(pair.second);
            }
        }
        textureCache.clear();
        beatmapPath.clear();
    }
}

std::vector<std::string> SkinManager::scanSkins(const std::string& skinsDir) {
    std::vector<std::string> skins;
    try {
        for (const auto& entry : fs::directory_iterator(skinsDir)) {
            if (entry.is_directory()) {
                std::string skinIni = entry.path().string() + "/skin.ini";
                if (fs::exists(skinIni)) {
                    skins.push_back(entry.path().filename().string());
                }
            }
        }
    } catch (...) {}
    return skins;
}

bool SkinManager::loadSkin(const std::string& path, SDL_Renderer* rend) {
    unloadSkin();
    this->renderer = rend;
    this->skinPath = path;

    std::string iniPath = path + "/skin.ini";
    if (!parseSkinIni(iniPath)) {
        return false;
    }

    loaded = true;
    return true;
}

bool SkinManager::parseSkinIni(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    std::string line, section;
    ManiaConfig* currentMania = nullptr;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '/' || line[0] == '#') continue;

        // Section header
        if (line[0] == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            if (section == "Mania") {
                currentMania = nullptr;
            }
            continue;
        }

        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) continue;

        std::string key = trim(line.substr(0, colonPos));
        std::string value = trim(line.substr(colonPos + 1));

        if (section == "General") {
            parseGeneralSection(key, value);
        }
        else if (section == "Colours") {
            parseColoursSection(key, value);
        }
        else if (section == "Fonts") {
            parseFontsSection(key, value);
        }
        else if (section == "Mania") {
            if (key == "Keys") {
                int keys = std::stoi(value);
                currentMania = config.getOrCreateManiaConfig(keys);
            }
            else if (currentMania) {
                parseManiaSection(key, value, currentMania);
            }
        }
    }
    return true;
}

void SkinManager::parseGeneralSection(const std::string& key, const std::string& value) {
    if (key == "Name") config.name = value;
    else if (key == "Author") config.author = value;
    else if (key == "Version") config.version = value;
}

void SkinManager::parseColoursSection(const std::string& key, const std::string& value) {
    // Combo colors: Combo1, Combo2, etc.
    if (key.find("Combo") == 0 && key.size() == 6) {
        config.comboColours.push_back(parseColor(value));
    }
    else if (key == "MenuGlow") config.menuGlow = parseColor(value);
    else if (key == "SliderBorder") config.sliderBorder = parseColor(value);
}

void SkinManager::parseFontsSection(const std::string& key, const std::string& value) {
    if (key == "HitCirclePrefix") config.hitCirclePrefix = value;
    else if (key == "HitCircleOverlap") config.hitCircleOverlap = std::stoi(value);
    else if (key == "ScorePrefix") config.scorePrefix = value;
    else if (key == "ComboPrefix") config.comboPrefix = value;
    else if (key == "ScoreOverlap") config.scoreOverlap = std::stoi(value);
    else if (key == "ComboOverlap") config.comboOverlap = std::stoi(value);
}

void SkinManager::parseManiaSection(const std::string& key, const std::string& value, ManiaConfig* cfg) {
    try {
    // Column configuration
    if (key == "ColumnWidth") {
        cfg->columnWidth.clear();
        for (const auto& w : split(value, ',')) {
            if (!w.empty()) {
                float width = std::stof(w);
                cfg->columnWidth.push_back(std::max(5.0f, std::min(100.0f, width)));
            }
        }
    }
    else if (key == "ColumnLineWidth") {
        cfg->columnLineWidth.clear();
        for (const auto& w : split(value, ',')) {
            if (!w.empty()) {
                float width = std::stof(w);  // Allow 0 to hide line
                cfg->columnLineWidth.push_back(width);
            }
        }
    }
    else if (key == "ColumnSpacing") {
        cfg->columnSpacing.clear();
        for (const auto& s : split(value, ',')) {
            if (!s.empty()) cfg->columnSpacing.push_back(std::stof(s));
        }
    }
    else if (key == "ColumnStart") cfg->columnStart = std::stof(value);
    else if (key == "ColumnRight") cfg->columnRight = std::stof(value);

    // Judgement and lighting
    else if (key == "JudgementLine") cfg->judgementLine = (value == "1" || value == "true");
    else if (key == "HitPosition") cfg->hitPosition = std::max(240, std::min(480, std::stoi(value)));
    else if (key == "LightPosition") cfg->lightPosition = std::stoi(value);
    else if (key == "LightFramePerSecond") cfg->lightFramePerSecond = std::max(24, std::stoi(value));

    // Display positions
    else if (key == "ComboPosition") cfg->comboPosition = std::stoi(value);
    else if (key == "ScorePosition") cfg->scorePosition = std::stoi(value);
    else if (key == "BarlineHeight") cfg->barlineHeight = std::stof(value);

    // Layout
    else if (key == "SpecialStyle") cfg->specialStyle = std::stoi(value);
    else if (key == "KeysUnderNotes") cfg->keysUnderNotes = (value == "1" || value == "true");
    else if (key == "StageSeparation") cfg->stageSeparation = std::max(5.0f, std::stof(value));
    else if (key == "WidthForNoteHeightScale") cfg->widthForNoteHeightScale = std::stof(value);
    else if (key == "UpsideDown") cfg->upsideDown = (value == "1" || value == "true");

    // Note images - NoteImage0, NoteImage1, etc.
    else if (key.find("NoteImage") == 0) {
        std::string suffix = key.substr(9);
        if (suffix.empty()) return;

        char lastChar = suffix.back();
        if (lastChar == 'H') {
            int col = std::stoi(suffix.substr(0, suffix.size() - 1));
            if (col >= (int)cfg->noteImageH.size()) cfg->noteImageH.resize(col + 1);
            cfg->noteImageH[col] = value;
        }
        else if (lastChar == 'L') {
            int col = std::stoi(suffix.substr(0, suffix.size() - 1));
            if (col >= (int)cfg->noteImageL.size()) cfg->noteImageL.resize(col + 1);
            cfg->noteImageL[col] = value;
        }
        else if (lastChar == 'T') {
            int col = std::stoi(suffix.substr(0, suffix.size() - 1));
            if (col >= (int)cfg->noteImageT.size()) cfg->noteImageT.resize(col + 1);
            cfg->noteImageT[col] = value;
        }
        else {
            int col = std::stoi(suffix);
            if (col >= (int)cfg->noteImage.size()) cfg->noteImage.resize(col + 1);
            cfg->noteImage[col] = value;
        }
    }

    // Key images - KeyImage0, KeyImage0D, etc.
    else if (key.find("KeyImage") == 0) {
        std::string suffix = key.substr(8);
        if (suffix.empty()) return;

        char lastChar = suffix.back();
        if (lastChar == 'D') {
            int col = std::stoi(suffix.substr(0, suffix.size() - 1));
            if (col >= (int)cfg->keyImageD.size()) cfg->keyImageD.resize(col + 1);
            cfg->keyImageD[col] = value;
        }
        else {
            int col = std::stoi(suffix);
            if (col >= (int)cfg->keyImage.size()) cfg->keyImage.resize(col + 1);
            cfg->keyImage[col] = value;
        }
    }

    // Stage images
    else if (key == "StageLeft") cfg->stageLeft = value;
    else if (key == "StageRight") cfg->stageRight = value;
    else if (key == "StageBottom") cfg->stageBottom = value;
    else if (key == "StageHint") cfg->stageHint = value;
    else if (key == "StageLight") cfg->stageLight = value;
    else if (key == "LightingN") cfg->lightingN = value;
    else if (key == "LightingL") cfg->lightingL = value;
    else if (key == "WarningArrow") cfg->warningArrow = value;

    // Column colors - Colour0, Colour1, etc.
    else if (key.find("Colour") == 0 && key.size() > 6) {
        std::string suffix = key.substr(6);

        if (suffix == "ColumnLine") {
            cfg->colourColumnLine = parseColor(value);
        }
        else if (suffix == "JudgementLine") {
            cfg->colourJudgementLine = parseColor(value);
        }
        else if (suffix == "Barline") {
            cfg->colourBarline = parseColor(value);
        }
        else if (suffix == "Break") {
            cfg->colourBreak = parseColor(value);
        }
        else if (suffix == "Hold") {
            cfg->colourHold = parseColor(value);
        }
        else if (suffix == "KeyWarning") {
            cfg->colourKeyWarning = parseColor(value);
        }
        else if (suffix.find("Light") == 0) {
            // ColourLight0, ColourLight1, etc.
            int col = std::stoi(suffix.substr(5));
            if (col >= (int)cfg->colourLight.size()) cfg->colourLight.resize(col + 1);
            cfg->colourLight[col] = parseColor(value);
        }
        else {
            // Colour1, Colour2, etc. - column colors (1-indexed in osu!)
            try {
                int col = std::stoi(suffix);
                if (col >= 1) {  // osu! uses 1-indexed
                    int idx = col - 1;  // Convert to 0-indexed
                    if (idx >= (int)cfg->colour.size()) cfg->colour.resize(idx + 1);
                    cfg->colour[idx] = parseColor(value);
                }
            } catch (...) {}
        }
    }

    // Lighting width configs
    else if (key == "LightingNWidth") {
        cfg->lightingNWidth.clear();
        for (const auto& w : split(value, ',')) {
            if (!w.empty()) cfg->lightingNWidth.push_back(std::stof(w));
        }
    }
    else if (key == "LightingLWidth") {
        cfg->lightingLWidth.clear();
        for (const auto& w : split(value, ',')) {
            if (!w.empty()) cfg->lightingLWidth.push_back(std::stof(w));
        }
    }

    // Note flip and body style configs
    else if (key.find("NoteFlipWhenUpsideDown") == 0) {
        std::string suffix = key.substr(22);
        if (!suffix.empty()) {
            int col = std::stoi(suffix);
            if (col >= (int)cfg->noteFlipWhenUpsideDown.size())
                cfg->noteFlipWhenUpsideDown.resize(col + 1, true);
            cfg->noteFlipWhenUpsideDown[col] = (value == "1" || value == "true");
        }
    }
    else if (key.find("NoteBodyStyle") == 0) {
        std::string suffix = key.substr(13);
        if (!suffix.empty()) {
            int col = std::stoi(suffix);
            if (col >= (int)cfg->noteBodyStyle.size())
                cfg->noteBodyStyle.resize(col + 1, 0);
            cfg->noteBodyStyle[col] = std::stoi(value);
        }
    }

    // Combo font config
    else if (key == "FontCombo") cfg->fontCombo = value;
    else if (key == "ComboOverlap") cfg->comboOverlap = std::stoi(value);

    } catch (...) {
        // Ignore parsing errors for invalid values
    }
}

// Find image file with various extensions
std::string SkinManager::findImageFile(const std::string& baseName) const {
    static const std::vector<std::string> extensions = {
        ".png", ".jpg", ".jpeg", ".bmp", ".gif",
        "@2x.png", "@2x.jpg", "@2x.jpeg"
    };

    // First, try beatmap folder if set
    if (!beatmapPath.empty()) {
        for (const auto& ext : extensions) {
            fs::path path = fs::path(beatmapPath) / (baseName + ext);
            if (fs::exists(path)) {
                return path.string();
            }
        }
        // Try without extension
        fs::path path = fs::path(beatmapPath) / baseName;
        if (fs::exists(path)) {
            return path.string();
        }
    }

    // Then try skin folder
    for (const auto& ext : extensions) {
        fs::path path = fs::path(skinPath) / (baseName + ext);
        if (fs::exists(path)) {
            return path.string();
        }
    }

    // Try without extension (file might already have extension in name)
    fs::path path = fs::path(skinPath) / baseName;
    if (fs::exists(path)) {
        return path.string();
    }

    SDL_Log("SkinManager: Image not found: %s (in %s)", baseName.c_str(), skinPath.c_str());
    return "";
}

// Load texture using stb_image
SDL_Texture* SkinManager::loadTexture(const std::string& name) {
    // Convert backslashes to forward slashes for cross-platform compatibility
    std::string normalizedName = name;
    std::replace(normalizedName.begin(), normalizedName.end(), '\\', '/');

    // Check cache first (use normalized name)
    auto it = textureCache.find(normalizedName);
    if (it != textureCache.end()) {
        return it->second;
    }

    std::string filepath = findImageFile(normalizedName);
    if (filepath.empty()) {
        textureCache[normalizedName] = nullptr;
        return nullptr;
    }

    int width, height, channels;
    unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 4);
    if (!data) {
        textureCache[normalizedName] = nullptr;
        return nullptr;
    }

    // Check if image exceeds GPU texture size limit (16384x16384)
    // For Percy-style LN skins, the body image is intentionally very tall
    // but only the top portion contains actual content. We crop to fit GPU limits.
    const int MAX_TEXTURE_SIZE = 16384;
    int finalHeight = height;
    int finalWidth = width;
    if (height > MAX_TEXTURE_SIZE) {
        finalHeight = MAX_TEXTURE_SIZE;
    }
    if (width > MAX_TEXTURE_SIZE) {
        finalWidth = MAX_TEXTURE_SIZE;
    }

    // Create SDL surface from pixel data (use cropped dimensions)
    SDL_Surface* surface = SDL_CreateSurface(finalWidth, finalHeight, SDL_PIXELFORMAT_RGBA32);
    SDL_Texture* texture = nullptr;

    if (!surface) {
        stbi_image_free(data);
        textureCache[normalizedName] = nullptr;
        return nullptr;
    }

    // Copy pixel data to surface (handle cropping)
    SDL_LockSurface(surface);
    if (finalWidth == width) {
        // No width cropping, can copy in one go
        memcpy(surface->pixels, data, finalWidth * finalHeight * 4);
    } else {
        // Width cropped, copy row by row
        for (int y = 0; y < finalHeight; y++) {
            memcpy((char*)surface->pixels + y * finalWidth * 4,
                   data + y * width * 4,
                   finalWidth * 4);
        }
    }
    SDL_UnlockSurface(surface);

    texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);

    stbi_image_free(data);
    textureCache[normalizedName] = texture;
    return texture;
}

// Get mania config for specified key count
const ManiaConfig* SkinManager::getManiaConfig(int keyCount) const {
    return config.getManiaConfig(keyCount);
}

// Get texture size
bool SkinManager::getTextureSize(SDL_Texture* tex, float* w, float* h) const {
    if (!tex) return false;
    int iw, ih;
    if (SDL_GetTextureSize(tex, w, h) == 0) {
        return true;
    }
    return false;
}

// Note texture getters
SDL_Texture* SkinManager::getNoteTexture(int column, int keyCount) const {
    const ManiaConfig* cfg = getManiaConfig(keyCount);
    if (cfg && column < (int)cfg->noteImage.size() && !cfg->noteImage[column].empty()) {
        return const_cast<SkinManager*>(this)->loadTexture(cfg->noteImage[column]);
    }
    // Default fallback: try mania-note1, mania-note2 based on column pattern
    // osu! uses alternating pattern: columns 0,2,4... use note1, columns 1,3,5... use note2
    std::string defaultName = "mania-note" + std::to_string((column % 2) + 1);
    return const_cast<SkinManager*>(this)->loadTexture(defaultName);
}

SDL_Texture* SkinManager::getNoteHeadTexture(int column, int keyCount) const {
    const ManiaConfig* cfg = getManiaConfig(keyCount);
    std::string headImageName;

    if (cfg && column < (int)cfg->noteImageH.size() && !cfg->noteImageH[column].empty()) {
        headImageName = cfg->noteImageH[column];
    } else {
        // Default fallback: mania-note1H, mania-note2H based on column pattern
        headImageName = "mania-note" + std::to_string((column % 2) + 1) + "H";
    }

    // Normalize path and check file size
    std::string normalizedName = headImageName;
    std::replace(normalizedName.begin(), normalizedName.end(), '\\', '/');
    std::string filepath = findImageFile(normalizedName);

    // If file exists and is too small (< 200 bytes), use body texture instead
    if (!filepath.empty()) {
        try {
            auto fileSize = fs::file_size(filepath);
            if (fileSize < 200) {
                static bool loggedOnce = false;
                if (!loggedOnce) {
                    SDL_Log("SkinManager: Head texture too small (%zu bytes), using body texture", fileSize);
                    loggedOnce = true;
                }
                return getNoteBodyTexture(column, keyCount);
            }
        } catch (...) {}
    }

    SDL_Texture* tex = const_cast<SkinManager*>(this)->loadTexture(headImageName);
    if (tex) return tex;

    // Final fallback to regular note
    return getNoteTexture(column, keyCount);
}

SDL_Texture* SkinManager::getNoteBodyTexture(int column, int keyCount) const {
    const ManiaConfig* cfg = getManiaConfig(keyCount);
    if (cfg && column < (int)cfg->noteImageL.size() && !cfg->noteImageL[column].empty()) {
        return const_cast<SkinManager*>(this)->loadTexture(cfg->noteImageL[column]);
    }
    // Default fallback: mania-note1L, mania-note2L based on column pattern
    std::string defaultName = "mania-note" + std::to_string((column % 2) + 1) + "L";
    return const_cast<SkinManager*>(this)->loadTexture(defaultName);
}

SDL_Texture* SkinManager::getNoteTailTexture(int column, int keyCount) const {
    const ManiaConfig* cfg = getManiaConfig(keyCount);
    if (cfg && column < (int)cfg->noteImageT.size() && !cfg->noteImageT[column].empty()) {
        return const_cast<SkinManager*>(this)->loadTexture(cfg->noteImageT[column]);
    }
    // Default fallback: mania-note1T, mania-note2T based on column pattern
    std::string defaultName = "mania-note" + std::to_string((column % 2) + 1) + "T";
    SDL_Texture* tex = const_cast<SkinManager*>(this)->loadTexture(defaultName);
    if (tex) return tex;
    // Final fallback to Hold head texture (not regular note)
    return getNoteHeadTexture(column, keyCount);
}

// Key texture getters
SDL_Texture* SkinManager::getKeyTexture(int column, int keyCount) const {
    const ManiaConfig* cfg = getManiaConfig(keyCount);
    if (cfg && column < (int)cfg->keyImage.size() && !cfg->keyImage[column].empty()) {
        return const_cast<SkinManager*>(this)->loadTexture(cfg->keyImage[column]);
    }
    // Default fallback: mania-key1, mania-key2 based on column pattern
    std::string defaultName = "mania-key" + std::to_string((column % 2) + 1);
    return const_cast<SkinManager*>(this)->loadTexture(defaultName);
}

SDL_Texture* SkinManager::getKeyDownTexture(int column, int keyCount) const {
    const ManiaConfig* cfg = getManiaConfig(keyCount);
    if (cfg && column < (int)cfg->keyImageD.size() && !cfg->keyImageD[column].empty()) {
        return const_cast<SkinManager*>(this)->loadTexture(cfg->keyImageD[column]);
    }
    // Default fallback: mania-key1D, mania-key2D based on column pattern
    std::string defaultName = "mania-key" + std::to_string((column % 2) + 1) + "D";
    SDL_Texture* tex = const_cast<SkinManager*>(this)->loadTexture(defaultName);
    if (tex) return tex;
    // Final fallback to key up texture
    return getKeyTexture(column, keyCount);
}

// Stage texture getters
SDL_Texture* SkinManager::getStageHintTexture(int keyCount) const {
    const ManiaConfig* cfg = getManiaConfig(keyCount);
    if (cfg && !cfg->stageHint.empty()) {
        return const_cast<SkinManager*>(this)->loadTexture(cfg->stageHint);
    }
    return const_cast<SkinManager*>(this)->loadTexture("mania-stage-hint");
}

SDL_Texture* SkinManager::getStageLeftTexture() const {
    return const_cast<SkinManager*>(this)->loadTexture("mania-stage-left");
}

SDL_Texture* SkinManager::getStageRightTexture() const {
    return const_cast<SkinManager*>(this)->loadTexture("mania-stage-right");
}

SDL_Texture* SkinManager::getStageBottomTexture() const {
    return const_cast<SkinManager*>(this)->loadTexture("mania-stage-bottom");
}

SDL_Texture* SkinManager::getStageLightTexture() const {
    return const_cast<SkinManager*>(this)->loadTexture("mania-stage-light");
}

// Lighting texture getters
SDL_Texture* SkinManager::getLightingNTexture(int keyCount) const {
    const ManiaConfig* cfg = getManiaConfig(keyCount);
    if (cfg && !cfg->lightingN.empty()) {
        return const_cast<SkinManager*>(this)->loadTexture(cfg->lightingN);
    }
    return const_cast<SkinManager*>(this)->loadTexture("lightingN");
}

SDL_Texture* SkinManager::getLightingLTexture(int keyCount) const {
    const ManiaConfig* cfg = getManiaConfig(keyCount);
    if (cfg && !cfg->lightingL.empty()) {
        return const_cast<SkinManager*>(this)->loadTexture(cfg->lightingL);
    }
    return const_cast<SkinManager*>(this)->loadTexture("lightingL");
}

// Judgement texture getter
SDL_Texture* SkinManager::getHitTexture(const std::string& judgement) const {
    // Try with -0 suffix first (osu! animation frame format)
    SDL_Texture* tex = const_cast<SkinManager*>(this)->loadTexture("mania-hit" + judgement + "-0");
    if (tex) return tex;
    // Fallback to without suffix
    return const_cast<SkinManager*>(this)->loadTexture("mania-hit" + judgement);
}

std::vector<SDL_Texture*> SkinManager::getHitTextureFrames(const std::string& judgement) const {
    std::vector<SDL_Texture*> frames;
    int frameIndex = 0;
    while (true) {
        SDL_Texture* tex = const_cast<SkinManager*>(this)->loadTexture(
            "mania-hit" + judgement + "-" + std::to_string(frameIndex));
        if (!tex) break;
        frames.push_back(tex);
        frameIndex++;
    }
    if (frames.empty()) {
        SDL_Texture* tex = const_cast<SkinManager*>(this)->loadTexture("mania-hit" + judgement);
        if (tex) frames.push_back(tex);
    }
    return frames;
}

int SkinManager::getHitTextureFrameCount(const std::string& judgement) const {
    return (int)getHitTextureFrames(judgement).size();
}

SDL_Texture* SkinManager::getComboDigitTexture(int digit) const {
    if (digit < 0 || digit > 9) return nullptr;
    return const_cast<SkinManager*>(this)->loadTexture("score-" + std::to_string(digit));
}

bool SkinManager::hasComboSkin() const {
    return getComboDigitTexture(0) != nullptr;
}

// Health bar texture getters
SDL_Texture* SkinManager::getScorebarBgTexture() const {
    return const_cast<SkinManager*>(this)->loadTexture("scorebar-bg");
}

SDL_Texture* SkinManager::getScorebarColourTexture(int frame) const {
    if (frame > 0) {
        // Try animation frame
        SDL_Texture* tex = const_cast<SkinManager*>(this)->loadTexture(
            "scorebar-colour-" + std::to_string(frame));
        if (tex) return tex;
    }
    // Try frame 0 or single texture
    SDL_Texture* tex = const_cast<SkinManager*>(this)->loadTexture("scorebar-colour-0");
    if (tex) return tex;
    return const_cast<SkinManager*>(this)->loadTexture("scorebar-colour");
}

int SkinManager::getScorebarColourFrameCount() const {
    int count = 0;
    while (true) {
        std::string name = "scorebar-colour-" + std::to_string(count);
        SDL_Texture* tex = const_cast<SkinManager*>(this)->loadTexture(name);
        if (!tex) break;
        count++;
    }
    return count > 0 ? count : 1;  // At least 1 frame
}

SDL_Texture* SkinManager::getScorebarKiTexture() const {
    return const_cast<SkinManager*>(this)->loadTexture("scorebar-ki");
}

SDL_Texture* SkinManager::getScorebarKiDangerTexture() const {
    return const_cast<SkinManager*>(this)->loadTexture("scorebar-kidanger");
}

SDL_Texture* SkinManager::getScorebarKiDanger2Texture() const {
    return const_cast<SkinManager*>(this)->loadTexture("scorebar-kidanger2");
}

SDL_Texture* SkinManager::getScorebarMarkerTexture() const {
    return const_cast<SkinManager*>(this)->loadTexture("scorebar-marker");
}
