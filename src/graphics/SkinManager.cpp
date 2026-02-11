#include "SkinManager.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <regex>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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
    frameCache.clear();  // Clear multi-frame cache (textures already destroyed above)
    noteHeadCache.clear();
    noteTailCache.clear();
    config = SkinConfig();
    skinPath = "";  // Clear skin path so findImageFile won't use old path
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
        frameCache.clear();
        noteHeadCache.clear();
        noteTailCache.clear();
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
        frameCache.clear();
        noteHeadCache.clear();
        noteTailCache.clear();
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
    else if (key == "AnimationFramerate") {
        try { config.animationFramerate = std::stoi(value); } catch (...) {}
    }
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
    const float SCALE = ManiaConfig::POSITION_SCALE_FACTOR;

    // Column configuration
    if (key == "ColumnWidth") {
        cfg->columnWidth.clear();
        for (const auto& w : split(value, ',')) {
            if (!w.empty()) {
                float width = 0;
                try { width = std::stof(w); } catch (...) { width = 0; }
                // Apply scale factor like lazer does
                cfg->columnWidth.push_back(width * SCALE);
            }
        }
    }
    else if (key == "ColumnLineWidth") {
        cfg->columnLineWidth.clear();
        for (const auto& w : split(value, ',')) {
            if (!w.empty()) {
                float width = 0;
                try { width = std::stof(w); } catch (...) { width = 0; }
                // ColumnLineWidth does NOT apply scale factor
                cfg->columnLineWidth.push_back(width);
            }
        }
    }
    else if (key == "ColumnSpacing") {
        cfg->columnSpacing.clear();
        for (const auto& s : split(value, ',')) {
            if (!s.empty()) {
                float spacing = 0;
                try { spacing = std::stof(s); } catch (...) { spacing = 0; }
                // Apply scale factor
                cfg->columnSpacing.push_back(spacing * SCALE);
            }
        }
    }
    else if (key == "ColumnStart") cfg->columnStart = std::stof(value);
    else if (key == "ColumnRight") cfg->columnRight = std::stof(value);

    // Judgement and lighting
    else if (key == "JudgementLine") cfg->judgementLine = (value == "1" || value == "true");
    else if (key == "HitPosition") {
        // lazer: (480 - clamp(value, 240, 480)) * SCALE
        float rawValue = std::stof(value);
        rawValue = std::max(240.0f, std::min(480.0f, rawValue));
        cfg->hitPosition = (480.0f - rawValue) * SCALE;
    }
    else if (key == "LightPosition") {
        // lazer: (480 - value) * SCALE
        float rawValue = std::stof(value);
        cfg->lightPosition = (480.0f - rawValue) * SCALE;
    }
    else if (key == "LightFramePerSecond") {
        int fps = std::stoi(value);
        cfg->lightFramePerSecond = fps > 0 ? fps : 24;
    }

    // Display positions (apply scale factor)
    else if (key == "ComboPosition") cfg->comboPosition = std::stof(value) * SCALE;
    else if (key == "ScorePosition") cfg->scorePosition = std::stof(value) * SCALE;
    else if (key == "BarlineHeight") cfg->barlineHeight = std::stof(value);  // No scale

    // Layout
    else if (key == "SpecialStyle") cfg->specialStyle = std::stoi(value);
    else if (key == "KeysUnderNotes") cfg->keysUnderNotes = (value == "1" || value == "true");
    else if (key == "StageSeparation") cfg->stageSeparation = std::max(5.0f, std::stof(value));
    else if (key == "WidthForNoteHeightScale") cfg->widthForNoteHeightScale = std::stof(value) * SCALE;
    else if (key == "UpsideDown") cfg->upsideDown = (value == "1" || value == "true");

    // Note images - store in imageLookups dictionary (like lazer)
    // NoteImage0, NoteImage1, NoteImage0H, NoteImage0L, NoteImage0T, etc.
    else if (key.find("NoteImage") == 0) {
        cfg->imageLookups[key] = value;

        // Also maintain backward compatibility with old vectors
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

    // Key images - store in imageLookups dictionary
    // KeyImage0, KeyImage1, KeyImage0D, etc.
    else if (key.find("KeyImage") == 0) {
        cfg->imageLookups[key] = value;

        // Also maintain backward compatibility
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

    // Stage images - store in imageLookups dictionary
    else if (key == "StageLeft") { cfg->stageLeft = value; cfg->imageLookups[key] = value; }
    else if (key == "StageRight") { cfg->stageRight = value; cfg->imageLookups[key] = value; }
    else if (key == "StageBottom") { cfg->stageBottom = value; cfg->imageLookups[key] = value; }
    else if (key == "StageHint") { cfg->stageHint = value; cfg->imageLookups[key] = value; }
    else if (key == "StageLight") { cfg->stageLight = value; cfg->imageLookups[key] = value; }
    else if (key == "LightingN") { cfg->lightingN = value; cfg->imageLookups[key] = value; }
    else if (key == "LightingL") { cfg->lightingL = value; cfg->imageLookups[key] = value; }
    else if (key == "WarningArrow") { cfg->warningArrow = value; cfg->imageLookups[key] = value; }

    // Hit images - store in imageLookups (Hit300g, Hit300, Hit200, Hit100, Hit50, Hit0)
    else if (key.find("Hit") == 0 && key.size() > 3) {
        cfg->imageLookups[key] = value;
    }

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
            // ColourLight1, ColourLight2, etc. (1-indexed in osu!)
            int col = std::stoi(suffix.substr(5));
            if (col >= 1) {
                int idx = col - 1;  // Convert to 0-indexed
                if (idx >= (int)cfg->colourLight.size()) cfg->colourLight.resize(idx + 1);
                cfg->colourLight[idx] = parseColor(value);
            }
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

    // Lighting width configs (apply scale factor like lazer)
    else if (key == "LightingNWidth") {
        cfg->explosionWidth.clear();
        for (const auto& w : split(value, ',')) {
            if (!w.empty()) {
                float width = 0;
                try { width = std::stof(w); } catch (...) { width = 0; }
                cfg->explosionWidth.push_back(width * SCALE);
            }
        }
    }
    else if (key == "LightingLWidth") {
        cfg->holdNoteLightWidth.clear();
        for (const auto& w : split(value, ',')) {
            if (!w.empty()) {
                float width = 0;
                try { width = std::stof(w); } catch (...) { width = 0; }
                cfg->holdNoteLightWidth.push_back(width * SCALE);
            }
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
    else if (key == "NoteBodyStyle") {
        // Global NoteBodyStyle (no column suffix)
        cfg->noteBodyStyle = std::stoi(value);
    }
    else if (key.find("NoteBodyStyle") == 0) {
        // Per-column NoteBodyStyle (NoteBodyStyle0, NoteBodyStyle1, etc.)
        std::string suffix = key.substr(13);
        if (!suffix.empty()) {
            int col = std::stoi(suffix);
            if (col >= (int)cfg->noteBodyStylePerColumn.size())
                cfg->noteBodyStylePerColumn.resize(col + 1, -1);
            cfg->noteBodyStylePerColumn[col] = std::stoi(value);
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
    // @2x versions first (high resolution priority)
    static const std::vector<std::string> extensions = {
        "@2x.png", "@2x.jpg", "@2x.jpeg",
        ".png", ".jpg", ".jpeg", ".bmp", ".gif"
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
        isHighResCache[normalizedName] = false;
        return nullptr;
    }

    // Check if this is a @2x texture
    bool isHighRes = filepath.find("@2x") != std::string::npos;

    int width, height, channels;
    unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 4);
    if (!data) {
        textureCache[normalizedName] = nullptr;
        isHighResCache[normalizedName] = false;
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
    isHighResCache[normalizedName] = isHighRes;
    return texture;
}

// Load multi-frame texture (osu! style: name-0, name-1, name-2...)
std::vector<SDL_Texture*> SkinManager::loadTextureFrames(const std::string& baseName) {
    std::string normalizedName = baseName;
    std::replace(normalizedName.begin(), normalizedName.end(), '\\', '/');

    // Check frame cache first
    auto it = frameCache.find(normalizedName);
    if (it != frameCache.end()) {
        return it->second;
    }

    std::vector<SDL_Texture*> frames;

    // Try loading frame 0 first (name-0)
    SDL_Texture* frame0 = loadTexture(normalizedName + "-0");

    if (frame0) {
        // Found frame 0, load all subsequent frames
        frames.push_back(frame0);
        int frameIndex = 1;
        while (true) {
            SDL_Texture* frame = loadTexture(normalizedName + "-" + std::to_string(frameIndex));
            if (!frame) break;
            frames.push_back(frame);
            frameIndex++;
        }
    } else {
        // No frame 0, try loading single texture
        SDL_Texture* single = loadTexture(normalizedName);
        if (single) {
            frames.push_back(single);
        }
    }

    frameCache[normalizedName] = frames;
    return frames;
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

// Get default column type for fallback textures (1, 2, or S)
// Based on osu! stable's default mania skin patterns
static std::string getDefaultColumnType(int column, int keyCount) {
    // osu! default column patterns by key count
    static const char* patterns[] = {
        "",                     // 0K (invalid)
        "S",                    // 1K
        "12",                   // 2K
        "1S2",                  // 3K
        "1221",                 // 4K
        "12S21",                // 5K
        "121121",               // 6K
        "121S121",              // 7K
        "1212S2121",            // 8K (with 2 special keys)
        "12121S12121",          // 9K
        "1212S1S2121",          // 10K
    };

    if (keyCount < 1 || keyCount > 10) {
        // Fallback to alternating pattern for unsupported key counts
        return std::to_string((column % 2) + 1);
    }

    const char* pattern = patterns[keyCount];
    if (column < 0 || column >= (int)strlen(pattern)) {
        return std::to_string((column % 2) + 1);
    }

    char type = pattern[column];
    if (type == 'S') return "S";
    return std::string(1, type);
}

// Note texture getters (with multi-frame animation support)
std::vector<SDL_Texture*> SkinManager::getNoteFrames(int column, int keyCount) const {
    const ManiaConfig* cfg = getManiaConfig(keyCount);
    std::string baseName;

    if (cfg && column < (int)cfg->noteImage.size() && !cfg->noteImage[column].empty()) {
        baseName = cfg->noteImage[column];
    } else {
        // Default fallback based on osu! column type pattern
        std::string colType = getDefaultColumnType(column, keyCount);
        baseName = "mania-note" + colType;
    }

    return const_cast<SkinManager*>(this)->loadTextureFrames(baseName);
}

int SkinManager::getNoteFrameCount(int column, int keyCount) const {
    return (int)getNoteFrames(column, keyCount).size();
}

SDL_Texture* SkinManager::getNoteTexture(int column, int keyCount, int frame) const {
    auto frames = getNoteFrames(column, keyCount);
    if (frames.empty()) return nullptr;
    if (frame < 0 || frame >= (int)frames.size()) frame = 0;
    return frames[frame];
}

std::vector<SDL_Texture*> SkinManager::getNoteHeadFrames(int column, int keyCount) const {
    // Check cache first
    std::string cacheKey = std::to_string(keyCount) + "_" + std::to_string(column);
    auto cacheIt = noteHeadCache.find(cacheKey);
    if (cacheIt != noteHeadCache.end()) {
        return cacheIt->second;
    }

    const ManiaConfig* cfg = getManiaConfig(keyCount);
    std::string baseName;

    if (cfg && column < (int)cfg->noteImageH.size() && !cfg->noteImageH[column].empty()) {
        baseName = cfg->noteImageH[column];
    } else {
        std::string colType = getDefaultColumnType(column, keyCount);
        baseName = "mania-note" + colType + "H";
    }

    auto frames = const_cast<SkinManager*>(this)->loadTextureFrames(baseName);

    // If head texture not found or too small, fallback to body texture
    if (frames.empty()) {
        auto result = getNoteBodyFrames(column, keyCount);
        noteHeadCache[cacheKey] = result;
        return result;
    }

    // Check if first frame file is too small (< 200 bytes)
    std::string normalizedName = baseName;
    std::replace(normalizedName.begin(), normalizedName.end(), '\\', '/');
    std::string filepath = findImageFile(normalizedName + "-0");
    if (filepath.empty()) {
        filepath = findImageFile(normalizedName);
    }
    if (!filepath.empty()) {
        try {
            auto fileSize = fs::file_size(filepath);
            if (fileSize < 200) {
                auto result = getNoteBodyFrames(column, keyCount);
                noteHeadCache[cacheKey] = result;
                return result;
            }
        } catch (...) {}
    }

    noteHeadCache[cacheKey] = frames;
    return frames;
}

int SkinManager::getNoteHeadFrameCount(int column, int keyCount) const {
    return (int)getNoteHeadFrames(column, keyCount).size();
}

SDL_Texture* SkinManager::getNoteHeadTexture(int column, int keyCount, int frame) const {
    auto frames = getNoteHeadFrames(column, keyCount);
    if (frames.empty()) {
        // Final fallback to regular note
        return getNoteTexture(column, keyCount, frame);
    }
    if (frame < 0 || frame >= (int)frames.size()) frame = 0;
    return frames[frame];
}

// Get note body texture frames (multi-frame animation support)
std::vector<SDL_Texture*> SkinManager::getNoteBodyFrames(int column, int keyCount) const {
    const ManiaConfig* cfg = getManiaConfig(keyCount);
    std::string baseName;

    if (cfg && column < (int)cfg->noteImageL.size() && !cfg->noteImageL[column].empty()) {
        baseName = cfg->noteImageL[column];
    } else {
        std::string colType = getDefaultColumnType(column, keyCount);
        baseName = "mania-note" + colType + "L";
    }

    return const_cast<SkinManager*>(this)->loadTextureFrames(baseName);
}

int SkinManager::getNoteBodyFrameCount(int column, int keyCount) const {
    return (int)getNoteBodyFrames(column, keyCount).size();
}

SDL_Texture* SkinManager::getNoteBodyTexture(int column, int keyCount, int frame) const {
    auto frames = getNoteBodyFrames(column, keyCount);
    if (frames.empty()) return nullptr;
    if (frame < 0 || frame >= (int)frames.size()) frame = 0;
    return frames[frame];
}

std::vector<SDL_Texture*> SkinManager::getNoteTailFrames(int column, int keyCount) const {
    // Check cache first
    std::string cacheKey = std::to_string(keyCount) + "_" + std::to_string(column);
    auto cacheIt = noteTailCache.find(cacheKey);
    if (cacheIt != noteTailCache.end()) {
        return cacheIt->second;
    }

    const ManiaConfig* cfg = getManiaConfig(keyCount);
    std::string baseName;

    if (cfg && column < (int)cfg->noteImageT.size() && !cfg->noteImageT[column].empty()) {
        baseName = cfg->noteImageT[column];
    } else {
        std::string colType = getDefaultColumnType(column, keyCount);
        baseName = "mania-note" + colType + "T";
    }

    auto frames = const_cast<SkinManager*>(this)->loadTextureFrames(baseName);

    // If tail texture not found, fallback to head texture
    if (frames.empty()) {
        auto result = getNoteHeadFrames(column, keyCount);
        noteTailCache[cacheKey] = result;
        return result;
    }

    noteTailCache[cacheKey] = frames;
    return frames;
}

int SkinManager::getNoteTailFrameCount(int column, int keyCount) const {
    return (int)getNoteTailFrames(column, keyCount).size();
}

SDL_Texture* SkinManager::getNoteTailTexture(int column, int keyCount, int frame) const {
    auto frames = getNoteTailFrames(column, keyCount);
    if (frames.empty()) return nullptr;
    if (frame < 0 || frame >= (int)frames.size()) frame = 0;
    return frames[frame];
}

// Animation frame interval calculation
float SkinManager::getNoteFrameInterval(int frameCount) const {
    if (config.animationFramerate > 0) {
        return 1000.0f / config.animationFramerate;
    }
    if (frameCount > 1) {
        return 1000.0f / frameCount;  // 1 second for all frames
    }
    return 16.666667f;  // Default ~60fps
}

// Key texture getters
SDL_Texture* SkinManager::getKeyTexture(int column, int keyCount) const {
    const ManiaConfig* cfg = getManiaConfig(keyCount);
    if (cfg && column < (int)cfg->keyImage.size() && !cfg->keyImage[column].empty()) {
        return const_cast<SkinManager*>(this)->loadTexture(cfg->keyImage[column]);
    }
    // Default fallback based on osu! column type pattern
    std::string colType = getDefaultColumnType(column, keyCount);
    std::string defaultName = "mania-key" + colType;
    return const_cast<SkinManager*>(this)->loadTexture(defaultName);
}

SDL_Texture* SkinManager::getKeyDownTexture(int column, int keyCount) const {
    const ManiaConfig* cfg = getManiaConfig(keyCount);
    if (cfg && column < (int)cfg->keyImageD.size() && !cfg->keyImageD[column].empty()) {
        return const_cast<SkinManager*>(this)->loadTexture(cfg->keyImageD[column]);
    }
    // Default fallback based on osu! column type pattern
    std::string colType = getDefaultColumnType(column, keyCount);
    std::string defaultName = "mania-key" + colType + "D";
    SDL_Texture* tex = const_cast<SkinManager*>(this)->loadTexture(defaultName);
    if (tex) return tex;
    // Final fallback to key up texture
    return getKeyTexture(column, keyCount);
}

bool SkinManager::hasCustomKeyImage(int keyCount) const {
    const ManiaConfig* cfg = getManiaConfig(keyCount);
    if (cfg && !cfg->keyImage.empty()) {
        for (const auto& img : cfg->keyImage) {
            if (!img.empty()) return true;
        }
    }
    return false;
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

// Lighting texture getters (with multi-frame animation support)
std::vector<SDL_Texture*> SkinManager::getLightingNFrames(int keyCount) const {
    const ManiaConfig* cfg = getManiaConfig(keyCount);
    std::string baseName;
    if (cfg && !cfg->lightingN.empty()) {
        baseName = cfg->lightingN;
    } else {
        baseName = "lightingN";
    }
    return const_cast<SkinManager*>(this)->loadTextureFrames(baseName);
}

int SkinManager::getLightingNFrameCount(int keyCount) const {
    return (int)getLightingNFrames(keyCount).size();
}

SDL_Texture* SkinManager::getLightingNTexture(int keyCount, int frame) const {
    auto frames = getLightingNFrames(keyCount);
    if (frames.empty()) return nullptr;
    if (frame < 0 || frame >= (int)frames.size()) frame = 0;
    return frames[frame];
}

std::vector<SDL_Texture*> SkinManager::getLightingLFrames(int keyCount) const {
    const ManiaConfig* cfg = getManiaConfig(keyCount);
    std::string baseName;
    if (cfg && !cfg->lightingL.empty()) {
        baseName = cfg->lightingL;
    } else {
        baseName = "lightingL";
    }
    auto frames = const_cast<SkinManager*>(this)->loadTextureFrames(baseName);

    // Fallback to LightingN if LightingL not found
    if (frames.empty()) {
        return getLightingNFrames(keyCount);
    }
    return frames;
}

int SkinManager::getLightingLFrameCount(int keyCount) const {
    return (int)getLightingLFrames(keyCount).size();
}

SDL_Texture* SkinManager::getLightingLTexture(int keyCount, int frame) const {
    auto frames = getLightingLFrames(keyCount);
    if (frames.empty()) return nullptr;
    if (frame < 0 || frame >= (int)frames.size()) frame = 0;
    return frames[frame];
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

bool SkinManager::hasCustomScorebarColour() const {
    // Check if scorebar-colour exists in skin folder (not default)
    std::string path = findImageFile("scorebar-colour");
    return !path.empty();
}

bool SkinManager::hasScorebarMarker() const {
    // Check if scorebar-marker exists in skin folder
    std::string path = findImageFile("scorebar-marker");
    return !path.empty();
}

bool SkinManager::isHighResTexture(SDL_Texture* tex) const {
    if (!tex) return false;
    for (const auto& pair : textureCache) {
        if (pair.second == tex) {
            auto it = isHighResCache.find(pair.first);
            return it != isHighResCache.end() && it->second;
        }
    }
    return false;
}

float SkinManager::getTextureScaleAdjust(SDL_Texture* tex) const {
    return isHighResTexture(tex) ? 2.0f : 1.0f;
}
