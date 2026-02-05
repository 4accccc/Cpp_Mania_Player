#pragma once
#include <SDL3/SDL.h>
#include <string>
#include <map>
#include <vector>
#include "SkinConfig.h"

class SkinManager {
public:
    SkinManager();
    ~SkinManager();

    // Skin loading and unloading
    bool loadSkin(const std::string& skinPath, SDL_Renderer* renderer);
    void unloadSkin();
    bool isLoaded() const { return loaded; }
    const std::string& getSkinPath() const { return skinPath; }

    // Beatmap folder skin support
    void setBeatmapPath(const std::string& path);
    void clearBeatmapPath();

    // Get skin config
    const SkinConfig& getConfig() const { return config; }
    const ManiaConfig* getManiaConfig(int keyCount) const;

    // Texture getters - Note related
    SDL_Texture* getNoteTexture(int column, int keyCount) const;
    SDL_Texture* getNoteHeadTexture(int column, int keyCount) const;
    SDL_Texture* getNoteBodyTexture(int column, int keyCount) const;
    SDL_Texture* getNoteTailTexture(int column, int keyCount) const;

    // Texture getters - Key related
    SDL_Texture* getKeyTexture(int column, int keyCount) const;
    SDL_Texture* getKeyDownTexture(int column, int keyCount) const;

    // Texture getters - Stage related
    SDL_Texture* getStageHintTexture(int keyCount) const;
    SDL_Texture* getStageLeftTexture() const;
    SDL_Texture* getStageRightTexture() const;
    SDL_Texture* getStageBottomTexture() const;
    SDL_Texture* getStageLightTexture() const;

    // Texture getters - Lighting related
    SDL_Texture* getLightingNTexture(int keyCount) const;
    SDL_Texture* getLightingLTexture(int keyCount) const;

    // Texture getters - Judgement related
    SDL_Texture* getHitTexture(const std::string& judgement) const;
    std::vector<SDL_Texture*> getHitTextureFrames(const std::string& judgement) const;
    int getHitTextureFrameCount(const std::string& judgement) const;

    // Texture getters - Combo number related
    SDL_Texture* getComboDigitTexture(int digit) const;  // 0-9
    bool hasComboSkin() const;

    // Texture getters - Health bar related
    SDL_Texture* getScorebarBgTexture() const;
    SDL_Texture* getScorebarColourTexture(int frame = 0) const;
    int getScorebarColourFrameCount() const;
    SDL_Texture* getScorebarKiTexture() const;
    SDL_Texture* getScorebarKiDangerTexture() const;
    SDL_Texture* getScorebarKiDanger2Texture() const;
    SDL_Texture* getScorebarMarkerTexture() const;

    // Get texture size
    bool getTextureSize(SDL_Texture* tex, float* w, float* h) const;

    // Scan available skins
    static std::vector<std::string> scanSkins(const std::string& skinsDir);

private:
    // skin.ini parsing
    bool parseSkinIni(const std::string& filepath);
    void parseGeneralSection(const std::string& key, const std::string& value);
    void parseColoursSection(const std::string& key, const std::string& value);
    void parseFontsSection(const std::string& key, const std::string& value);
    void parseManiaSection(const std::string& key, const std::string& value, ManiaConfig* cfg);

    // Texture loading
    SDL_Texture* loadTexture(const std::string& name);
    std::string findImageFile(const std::string& baseName) const;

    // Helper functions
    static std::string trim(const std::string& str);
    static std::vector<std::string> split(const std::string& str, char delim);
    static SDL_Color parseColor(const std::string& str);

    // Member variables
    SkinConfig config;
    std::string skinPath;
    std::string beatmapPath;  // Beatmap folder path for beatmap-specific skins
    SDL_Renderer* renderer = nullptr;
    bool loaded = false;

    // Texture cache
    mutable std::map<std::string, SDL_Texture*> textureCache;
};
