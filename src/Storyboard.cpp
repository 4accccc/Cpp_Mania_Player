#include "Storyboard.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

#define _USE_MATH_DEFINES
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define STB_IMAGE_IMPLEMENTATION_SKIP
#include "stb_image.h"

namespace fs = std::filesystem;

// ============== Easing Functions ==============

float Easing::bounceOut(float t) {
    const float n1 = 7.5625f;
    const float d1 = 2.75f;

    if (t < 1.0f / d1) {
        return n1 * t * t;
    } else if (t < 2.0f / d1) {
        t -= 1.5f / d1;
        return n1 * t * t + 0.75f;
    } else if (t < 2.5f / d1) {
        t -= 2.25f / d1;
        return n1 * t * t + 0.9375f;
    } else {
        t -= 2.625f / d1;
        return n1 * t * t + 0.984375f;
    }
}

float Easing::elasticOut(float t) {
    if (t == 0 || t == 1) return t;
    return (float)(pow(2.0, -10.0 * t) * sin((t * 10.0 - 0.75) * (2.0 * M_PI) / 3.0) + 1.0);
}

float Easing::elasticIn(float t) {
    if (t == 0 || t == 1) return t;
    return (float)(-(pow(2.0, 10.0 * t - 10.0) * sin((t * 10.0 - 10.75) * (2.0 * M_PI) / 3.0)));
}

float Easing::apply(EasingType type, float t) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;

    switch (type) {
        case EasingType::Linear:
            return t;

        case EasingType::EasingOut:
        case EasingType::QuadOut:
            return t * (2.0f - t);

        case EasingType::EasingIn:
        case EasingType::QuadIn:
            return t * t;

        case EasingType::QuadInOut:
            return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;

        case EasingType::CubicIn:
            return t * t * t;

        case EasingType::CubicOut:
            return 1.0f - (float)pow(1.0 - t, 3);

        case EasingType::CubicInOut:
            return t < 0.5f ? 4.0f * t * t * t : 1.0f - (float)pow(-2.0 * t + 2.0, 3) / 2.0f;

        case EasingType::QuartIn:
            return t * t * t * t;

        case EasingType::QuartOut:
            return 1.0f - (float)pow(1.0 - t, 4);

        case EasingType::QuartInOut:
            return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - (float)pow(-2.0 * t + 2.0, 4) / 2.0f;

        case EasingType::QuintIn:
            return t * t * t * t * t;

        case EasingType::QuintOut:
            return 1.0f - (float)pow(1.0 - t, 5);

        case EasingType::QuintInOut:
            return t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f - (float)pow(-2.0 * t + 2.0, 5) / 2.0f;

        case EasingType::SineIn:
            return 1.0f - (float)cos(t * M_PI / 2.0);

        case EasingType::SineOut:
            return (float)sin(t * M_PI / 2.0);

        case EasingType::SineInOut:
            return (float)(-(cos(M_PI * t) - 1.0) / 2.0);

        case EasingType::ExpoIn:
            return t == 0 ? 0 : (float)pow(2.0, 10.0 * t - 10.0);

        case EasingType::ExpoOut:
            return t == 1 ? 1 : 1.0f - (float)pow(2.0, -10.0 * t);

        case EasingType::ExpoInOut:
            if (t == 0) return 0;
            if (t == 1) return 1;
            return t < 0.5f ? (float)(pow(2.0, 20.0 * t - 10.0) / 2.0)
                           : (float)((2.0 - pow(2.0, -20.0 * t + 10.0)) / 2.0);

        case EasingType::CircIn:
            return 1.0f - (float)sqrt(1.0 - t * t);

        case EasingType::CircOut:
            return (float)sqrt(1.0 - pow(t - 1.0, 2));

        case EasingType::CircInOut:
            return t < 0.5f ? (float)((1.0 - sqrt(1.0 - pow(2.0 * t, 2))) / 2.0)
                           : (float)((sqrt(1.0 - pow(-2.0 * t + 2.0, 2)) + 1.0) / 2.0);

        case EasingType::ElasticIn:
            return elasticIn(t);

        case EasingType::ElasticOut:
            return elasticOut(t);

        case EasingType::ElasticHalfOut:
            if (t == 0 || t == 1) return t;
            return (float)(pow(2.0, -10.0 * t) * sin((0.5 * t * 10.0 - 0.75) * (2.0 * M_PI) / 3.0) + 1.0);

        case EasingType::ElasticQuarterOut:
            if (t == 0 || t == 1) return t;
            return (float)(pow(2.0, -10.0 * t) * sin((0.25 * t * 10.0 - 0.75) * (2.0 * M_PI) / 3.0) + 1.0);

        case EasingType::ElasticInOut:
            if (t == 0 || t == 1) return t;
            return t < 0.5f ? (float)(-(pow(2.0, 20.0 * t - 10.0) * sin((20.0 * t - 11.125) * (2.0 * M_PI) / 4.5)) / 2.0)
                           : (float)((pow(2.0, -20.0 * t + 10.0) * sin((20.0 * t - 11.125) * (2.0 * M_PI) / 4.5)) / 2.0 + 1.0);

        case EasingType::BackIn: {
            const float c1 = 1.70158f;
            const float c3 = c1 + 1.0f;
            return c3 * t * t * t - c1 * t * t;
        }

        case EasingType::BackOut: {
            const float c1 = 1.70158f;
            const float c3 = c1 + 1.0f;
            return 1.0f + c3 * (float)pow(t - 1.0, 3) + c1 * (float)pow(t - 1.0, 2);
        }

        case EasingType::BackInOut: {
            const float c1 = 1.70158f;
            const float c2 = c1 * 1.525f;
            return t < 0.5f ? (float)((pow(2.0 * t, 2) * ((c2 + 1.0) * 2.0 * t - c2)) / 2.0)
                           : (float)((pow(2.0 * t - 2.0, 2) * ((c2 + 1.0) * (t * 2.0 - 2.0) + c2) + 2.0) / 2.0);
        }

        case EasingType::BounceIn:
            return 1.0f - bounceOut(1.0f - t);

        case EasingType::BounceOut:
            return bounceOut(t);

        case EasingType::BounceInOut:
            return t < 0.5f ? (1.0f - bounceOut(1.0f - 2.0f * t)) / 2.0f
                           : (1.0f + bounceOut(2.0f * t - 1.0f)) / 2.0f;

        default:
            return t;
    }
}

// ============== Helper Functions ==============

std::string Storyboard::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> Storyboard::split(const std::string& str, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delim)) {
        result.push_back(token);
    }
    return result;
}

// ============== StoryboardSprite Implementation ==============

void StoryboardSprite::getOriginOffset(float texW, float texH, float& ox, float& oy) const {
    switch (origin) {
        case StoryboardOrigin::TopLeft:
            ox = 0; oy = 0; break;
        case StoryboardOrigin::Centre:
            ox = texW / 2; oy = texH / 2; break;
        case StoryboardOrigin::CentreLeft:
            ox = 0; oy = texH / 2; break;
        case StoryboardOrigin::TopRight:
            ox = texW; oy = 0; break;
        case StoryboardOrigin::BottomCentre:
            ox = texW / 2; oy = texH; break;
        case StoryboardOrigin::TopCentre:
            ox = texW / 2; oy = 0; break;
        case StoryboardOrigin::CentreRight:
            ox = texW; oy = texH / 2; break;
        case StoryboardOrigin::BottomLeft:
            ox = 0; oy = texH; break;
        case StoryboardOrigin::BottomRight:
            ox = texW; oy = texH; break;
        default:
            ox = texW / 2; oy = texH / 2; break;
    }
}

static float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

void StoryboardSprite::update(int64_t currentTime) {
    // Check if in active time range
    if (currentTime < startTime || currentTime > endTime) {
        visible = false;
        return;
    }

    // Reset to initial/default state each frame
    currentX = x;
    currentY = y;
    currentScaleX = currentScaleY = 1.0f;
    currentRotation = 0;
    currentOpacity = 1.0f;  // Default opacity = 1 (visible)
    currentR = currentG = currentB = 255;  // Default color = white
    flipH = flipV = false;
    additive = false;
    visible = true;

    // Pre-set initial values from first command of each type (osu! behavior)
    bool hasMove = false, hasMoveX = false, hasMoveY = false;
    bool hasScale = false, hasVScale = false, hasRotate = false;
    bool hasFade = false, hasColour = false;

    for (const auto& cmd : commands) {
        if (cmd.type == StoryboardCommandType::Loop) continue;

        switch (cmd.type) {
            case StoryboardCommandType::Move:
                if (!hasMove && !hasMoveX && !hasMoveY) {
                    currentX = cmd.startValue[0];
                    currentY = cmd.startValue[1];
                    hasMove = true;
                }
                break;
            case StoryboardCommandType::MoveX:
                if (!hasMove && !hasMoveX) {
                    currentX = cmd.startValue[0];
                    hasMoveX = true;
                }
                break;
            case StoryboardCommandType::MoveY:
                if (!hasMove && !hasMoveY) {
                    currentY = cmd.startValue[0];
                    hasMoveY = true;
                }
                break;
            case StoryboardCommandType::Scale:
                if (!hasScale && !hasVScale) {
                    currentScaleX = currentScaleY = cmd.startValue[0];
                    hasScale = true;
                }
                break;
            case StoryboardCommandType::VectorScale:
                if (!hasScale && !hasVScale) {
                    currentScaleX = cmd.startValue[0];
                    currentScaleY = cmd.startValue[1];
                    hasVScale = true;
                }
                break;
            case StoryboardCommandType::Rotate:
                if (!hasRotate) {
                    currentRotation = cmd.startValue[0];
                    hasRotate = true;
                }
                break;
            case StoryboardCommandType::Fade:
                if (!hasFade) {
                    currentOpacity = cmd.startValue[0];
                    hasFade = true;
                }
                break;
            case StoryboardCommandType::Colour:
                if (!hasColour) {
                    currentR = (uint8_t)cmd.startValue[0];
                    currentG = (uint8_t)cmd.startValue[1];
                    currentB = (uint8_t)cmd.startValue[2];
                    hasColour = true;
                }
                break;
            default:
                break;
        }
    }

    // Now process commands normally
    for (const auto& cmd : commands) {
        // Handle Loop command
        if (cmd.type == StoryboardCommandType::Loop) {
            int64_t loopDuration = 0;
            for (const auto& lc : cmd.loopCommands) {
                loopDuration = std::max(loopDuration, lc.endTime);
            }
            if (loopDuration <= 0) continue;

            int64_t relativeTime = currentTime - cmd.startTime;
            if (relativeTime < 0) continue;

            int iteration = (int)(relativeTime / loopDuration);
            if (cmd.loopCount > 0 && iteration >= cmd.loopCount) {
                iteration = cmd.loopCount - 1;
                relativeTime = loopDuration;
            } else {
                relativeTime = relativeTime % loopDuration;
            }

            for (const auto& lc : cmd.loopCommands) {
                float t = 0;
                if (relativeTime < lc.startTime) {
                    // Before command: use start value
                    t = 0;
                } else if (relativeTime >= lc.endTime) {
                    // After command: use end value (persist)
                    t = 1.0f;
                } else {
                    // During command: interpolate
                    if (lc.endTime > lc.startTime) {
                        t = (float)(relativeTime - lc.startTime) / (lc.endTime - lc.startTime);
                    } else {
                        t = 1.0f;
                    }
                }
                float easedT = Easing::apply(lc.easing, t);

                switch (lc.type) {
                    case StoryboardCommandType::Fade:
                        currentOpacity = lerp(lc.startValue[0], lc.endValue[0], easedT);
                        break;
                    case StoryboardCommandType::Move:
                        currentX = lerp(lc.startValue[0], lc.endValue[0], easedT);
                        currentY = lerp(lc.startValue[1], lc.endValue[1], easedT);
                        break;
                    case StoryboardCommandType::Scale:
                        currentScaleX = currentScaleY = lerp(lc.startValue[0], lc.endValue[0], easedT);
                        break;
                    case StoryboardCommandType::VectorScale:
                        currentScaleX = lerp(lc.startValue[0], lc.endValue[0], easedT);
                        currentScaleY = lerp(lc.startValue[1], lc.endValue[1], easedT);
                        break;
                    case StoryboardCommandType::Rotate:
                        currentRotation = lerp(lc.startValue[0], lc.endValue[0], easedT);
                        break;
                    case StoryboardCommandType::Colour:
                        currentR = (uint8_t)lerp(lc.startValue[0], lc.endValue[0], easedT);
                        currentG = (uint8_t)lerp(lc.startValue[1], lc.endValue[1], easedT);
                        currentB = (uint8_t)lerp(lc.startValue[2], lc.endValue[2], easedT);
                        break;
                    case StoryboardCommandType::MoveX:
                        currentX = lerp(lc.startValue[0], lc.endValue[0], easedT);
                        break;
                    case StoryboardCommandType::MoveY:
                        currentY = lerp(lc.startValue[0], lc.endValue[0], easedT);
                        break;
                    case StoryboardCommandType::Parameter:
                        if (relativeTime >= lc.startTime && relativeTime <= lc.endTime) {
                            if (lc.parameter == StoryboardParameter::FlipH) flipH = true;
                            else if (lc.parameter == StoryboardParameter::FlipV) flipV = true;
                            else if (lc.parameter == StoryboardParameter::Additive) additive = true;
                        }
                        break;
                    default:
                        break;
                }
            }
            continue;
        }

        // Normal command handling
        float t = 0;
        if (currentTime < cmd.startTime) {
            // Before command starts: skip (don't apply yet)
            continue;
        } else if (currentTime >= cmd.endTime) {
            // After command ends: use end value (persist)
            t = 1.0f;
        } else {
            // During command: interpolate
            if (cmd.endTime > cmd.startTime) {
                t = (float)(currentTime - cmd.startTime) / (cmd.endTime - cmd.startTime);
            } else {
                t = 1.0f;
            }
        }
        float easedT = Easing::apply(cmd.easing, t);

        switch (cmd.type) {
            case StoryboardCommandType::Fade:
                currentOpacity = lerp(cmd.startValue[0], cmd.endValue[0], easedT);
                break;
            case StoryboardCommandType::Move:
                currentX = lerp(cmd.startValue[0], cmd.endValue[0], easedT);
                currentY = lerp(cmd.startValue[1], cmd.endValue[1], easedT);
                break;
            case StoryboardCommandType::Scale:
                currentScaleX = currentScaleY = lerp(cmd.startValue[0], cmd.endValue[0], easedT);
                break;
            case StoryboardCommandType::VectorScale:
                currentScaleX = lerp(cmd.startValue[0], cmd.endValue[0], easedT);
                currentScaleY = lerp(cmd.startValue[1], cmd.endValue[1], easedT);
                break;
            case StoryboardCommandType::Rotate:
                currentRotation = lerp(cmd.startValue[0], cmd.endValue[0], easedT);
                break;
            case StoryboardCommandType::Colour:
                currentR = (uint8_t)lerp(cmd.startValue[0], cmd.endValue[0], easedT);
                currentG = (uint8_t)lerp(cmd.startValue[1], cmd.endValue[1], easedT);
                currentB = (uint8_t)lerp(cmd.startValue[2], cmd.endValue[2], easedT);
                break;
            case StoryboardCommandType::MoveX:
                currentX = lerp(cmd.startValue[0], cmd.endValue[0], easedT);
                break;
            case StoryboardCommandType::MoveY:
                currentY = lerp(cmd.startValue[0], cmd.endValue[0], easedT);
                break;
            case StoryboardCommandType::Parameter:
                if (currentTime >= cmd.startTime && currentTime <= cmd.endTime) {
                    if (cmd.parameter == StoryboardParameter::FlipH) flipH = true;
                    else if (cmd.parameter == StoryboardParameter::FlipV) flipV = true;
                    else if (cmd.parameter == StoryboardParameter::Additive) additive = true;
                }
                break;
            default:
                break;
        }
    }

    // Visibility check
    if (currentOpacity <= 0.001f) {
        visible = false;
    }
}

// ============== StoryboardAnimation Implementation ==============

int StoryboardAnimation::getCurrentFrame(int64_t currentTime) const {
    if (frameCount <= 1 || frameDelay <= 0) return 0;

    int64_t elapsed = currentTime - animStartTime;
    if (elapsed < 0) return 0;

    int frame = (int)(elapsed / frameDelay);
    if (loopForever) {
        return frame % frameCount;
    } else {
        return std::min(frame, frameCount - 1);
    }
}

void StoryboardAnimation::update(int64_t currentTime) {
    StoryboardSprite::update(currentTime);
    if (animStartTime == 0) {
        animStartTime = startTime;
    }
}

// ============== Storyboard Main Class Implementation ==============

Storyboard::Storyboard() {}

Storyboard::~Storyboard() {
    unloadTextures();
}

void Storyboard::setBeatmapDirectory(const std::string& dir) {
    beatmapDir = dir;
}

void Storyboard::clear() {
    unloadTextures();
    backgroundImage.clear();
    backgroundX = backgroundY = 0;
    for (int i = 0; i < 5; i++) {
        sprites[i].clear();
    }
}

bool Storyboard::hasStoryboard() const {
    for (int i = 0; i < 5; i++) {
        if (!sprites[i].empty()) return true;
    }
    return false;
}

bool Storyboard::hasBackground() const {
    return !backgroundImage.empty();
}

std::string Storyboard::findImageFile(const std::string& baseName) const {
    static const std::vector<std::string> extensions = {
        "", ".png", ".jpg", ".jpeg", ".bmp", ".gif"
    };

    for (const auto& ext : extensions) {
        fs::path path = fs::path(beatmapDir) / (baseName + ext);
        if (fs::exists(path)) {
            return path.string();
        }
    }
    return "";
}

SDL_Texture* Storyboard::loadTexture(const std::string& name) {
    if (name.empty() || !sdlRenderer) return nullptr;

    auto it = textureCache.find(name);
    if (it != textureCache.end()) {
        return it->second;
    }

    std::string filepath = findImageFile(name);
    if (filepath.empty()) {
        textureCache[name] = nullptr;
        return nullptr;
    }

    int width, height, channels;
    unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 4);
    if (!data) {
        textureCache[name] = nullptr;
        return nullptr;
    }

    SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (surface) {
        SDL_LockSurface(surface);
        memcpy(surface->pixels, data, width * height * 4);
        SDL_UnlockSurface(surface);
    }
    stbi_image_free(data);

    if (!surface) {
        textureCache[name] = nullptr;
        return nullptr;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(sdlRenderer, surface);
    SDL_DestroySurface(surface);

    textureCache[name] = texture;
    return texture;
}

void Storyboard::loadTextures(SDL_Renderer* renderer) {
    sdlRenderer = renderer;

    // Load background texture
    if (!backgroundImage.empty()) {
        backgroundTexture = loadTexture(backgroundImage);
    }

    // Load all sprite textures
    for (int i = 0; i < 5; i++) {
        for (auto& sprite : sprites[i]) {
            sprite->texture = loadTexture(sprite->filepath);
        }
    }
}

void Storyboard::unloadTextures() {
    for (auto& pair : textureCache) {
        if (pair.second) {
            SDL_DestroyTexture(pair.second);
        }
    }
    textureCache.clear();
    backgroundTexture = nullptr;

    for (int i = 0; i < 5; i++) {
        for (auto& sprite : sprites[i]) {
            sprite->texture = nullptr;
        }
    }
}

bool Storyboard::loadFromOsu(const std::string& osuPath) {
    return parseEvents(osuPath);
}

bool Storyboard::loadFromOsb(const std::string& osbPath) {
    if (!fs::exists(osbPath)) return false;
    return parseEvents(osbPath);
}

bool Storyboard::parseEvents(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    std::string line;
    std::string section;
    StoryboardSprite* currentSprite = nullptr;
    bool inLoop = false;
    bool inTrigger = false;  // Skip trigger blocks
    StoryboardCommand currentLoop;

    while (std::getline(file, line)) {
        // Remove BOM
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) {
            line = line.substr(3);
        }

        std::string trimmedLine = trim(line);
        if (trimmedLine.empty() || trimmedLine[0] == '/') continue;

        // Check section
        if (trimmedLine[0] == '[' && trimmedLine.back() == ']') {
            section = trimmedLine.substr(1, trimmedLine.size() - 2);
            continue;
        }

        if (section != "Events") continue;

        // Check indent level
        int indent = 0;
        for (char c : line) {
            if (c == ' ' || c == '_') indent++;
            else break;
        }

        // Background image: 0,0,"filename",x,y
        if (indent == 0 && trimmedLine[0] == '0') {
            auto parts = split(trimmedLine, ',');
            if (parts.size() >= 3) {
                std::string fn = trim(parts[2]);
                if (fn.size() >= 2 && fn.front() == '"' && fn.back() == '"') {
                    fn = fn.substr(1, fn.size() - 2);
                }
                backgroundImage = fn;
                if (parts.size() >= 5) {
                    try {
                        backgroundX = std::stoi(parts[3]);
                        backgroundY = std::stoi(parts[4]);
                    } catch (...) {}
                }
            }
            continue;
        }

        // Sprite or Animation
        if (indent == 0 && (trimmedLine.rfind("Sprite,", 0) == 0 || trimmedLine.rfind("4,", 0) == 0)) {
            currentSprite = parseSprite(trimmedLine);
            inLoop = false;
            continue;
        }

        if (indent == 0 && (trimmedLine.rfind("Animation,", 0) == 0 || trimmedLine.rfind("6,", 0) == 0)) {
            currentSprite = parseAnimation(trimmedLine);
            inLoop = false;
            continue;
        }

        // Command parsing
        if (currentSprite && indent > 0) {
            // Loop command start
            if (trimmedLine[0] == 'L') {
                inTrigger = false;  // End any trigger block
                auto parts = split(trimmedLine, ',');
                if (parts.size() >= 3) {
                    currentLoop = StoryboardCommand();
                    currentLoop.type = StoryboardCommandType::Loop;
                    try {
                        currentLoop.startTime = std::stoll(parts[1]);
                        currentLoop.loopCount = std::stoi(parts[2]);
                    } catch (...) {}
                    inLoop = true;
                }
                continue;
            }

            // Trigger command start - skip entire block
            if (trimmedLine[0] == 'T') {
                inTrigger = true;
                inLoop = false;
                continue;
            }

            // Skip commands inside Trigger block
            if (inTrigger && indent >= 2) {
                continue;
            }

            // End trigger block when back to indent 1
            if (inTrigger && indent == 1) {
                inTrigger = false;
            }

            // Commands inside Loop
            if (inLoop && indent >= 2) {
                StoryboardCommand cmd = parseCommandLine(trimmedLine);
                currentLoop.loopCommands.push_back(cmd);
                continue;
            }

            // Normal command or Loop end
            if (inLoop && indent == 1) {
                // Save previous Loop
                currentSprite->commands.push_back(currentLoop);
                inLoop = false;
            }

            // Parse normal command
            StoryboardCommand cmd = parseCommandLine(trimmedLine);
            currentSprite->commands.push_back(cmd);
        }
    }

    // Save last Loop
    if (inLoop && currentSprite) {
        currentSprite->commands.push_back(currentLoop);
    }

    // Calculate startTime and endTime for all sprites
    for (int i = 0; i < 5; i++) {
        for (auto& sprite : sprites[i]) {
            int64_t minTime = INT64_MAX;
            int64_t maxTime = INT64_MIN;
            for (const auto& cmd : sprite->commands) {
                if (cmd.type == StoryboardCommandType::Loop) {
                    // For loops, calculate total duration
                    int64_t loopDuration = 0;
                    for (const auto& lc : cmd.loopCommands) {
                        loopDuration = std::max(loopDuration, lc.endTime);
                    }
                    minTime = std::min(minTime, cmd.startTime);
                    maxTime = std::max(maxTime, cmd.startTime + loopDuration * cmd.loopCount);
                } else {
                    minTime = std::min(minTime, cmd.startTime);
                    maxTime = std::max(maxTime, cmd.endTime);
                }
            }
            if (minTime != INT64_MAX) sprite->startTime = minTime;
            if (maxTime != INT64_MIN) sprite->endTime = maxTime;
        }
    }

    return true;
}

StoryboardSprite* Storyboard::parseSprite(const std::string& line) {
    // Format: Sprite,layer,origin,"filepath",x,y
    auto parts = split(line, ',');
    if (parts.size() < 6) return nullptr;

    auto sprite = std::make_unique<StoryboardSprite>();

    try {
        // Layer
        std::string layerStr = trim(parts[1]);
        if (layerStr == "Background" || layerStr == "0") sprite->layer = StoryboardLayer::Background;
        else if (layerStr == "Fail" || layerStr == "1") sprite->layer = StoryboardLayer::Fail;
        else if (layerStr == "Pass" || layerStr == "2") sprite->layer = StoryboardLayer::Pass;
        else if (layerStr == "Foreground" || layerStr == "3") sprite->layer = StoryboardLayer::Foreground;
        else if (layerStr == "Overlay" || layerStr == "4") sprite->layer = StoryboardLayer::Overlay;

        // Origin
        std::string originStr = trim(parts[2]);
        if (originStr == "TopLeft" || originStr == "0") sprite->origin = StoryboardOrigin::TopLeft;
        else if (originStr == "Centre" || originStr == "1") sprite->origin = StoryboardOrigin::Centre;
        else if (originStr == "CentreLeft" || originStr == "2") sprite->origin = StoryboardOrigin::CentreLeft;
        else if (originStr == "TopRight" || originStr == "3") sprite->origin = StoryboardOrigin::TopRight;
        else if (originStr == "BottomCentre" || originStr == "4") sprite->origin = StoryboardOrigin::BottomCentre;
        else if (originStr == "TopCentre" || originStr == "5") sprite->origin = StoryboardOrigin::TopCentre;
        else if (originStr == "CentreRight" || originStr == "7") sprite->origin = StoryboardOrigin::CentreRight;
        else if (originStr == "BottomLeft" || originStr == "8") sprite->origin = StoryboardOrigin::BottomLeft;
        else if (originStr == "BottomRight" || originStr == "9") sprite->origin = StoryboardOrigin::BottomRight;

        // Filepath
        std::string fn = trim(parts[3]);
        if (fn.size() >= 2 && fn.front() == '"' && fn.back() == '"') {
            fn = fn.substr(1, fn.size() - 2);
        }
        sprite->filepath = fn;

        // Position
        sprite->x = std::stof(parts[4]);
        sprite->y = std::stof(parts[5]);
    } catch (...) {
        return nullptr;
    }

    int layerIdx = static_cast<int>(sprite->layer);
    StoryboardSprite* ptr = sprite.get();
    sprites[layerIdx].push_back(std::move(sprite));
    return ptr;
}

StoryboardAnimation* Storyboard::parseAnimation(const std::string& line) {
    // Format: Animation,layer,origin,"filepath",x,y,frameCount,frameDelay,loopType
    auto parts = split(line, ',');
    if (parts.size() < 8) return nullptr;

    auto anim = std::make_unique<StoryboardAnimation>();

    try {
        // Layer
        std::string layerStr = trim(parts[1]);
        if (layerStr == "Background" || layerStr == "0") anim->layer = StoryboardLayer::Background;
        else if (layerStr == "Fail" || layerStr == "1") anim->layer = StoryboardLayer::Fail;
        else if (layerStr == "Pass" || layerStr == "2") anim->layer = StoryboardLayer::Pass;
        else if (layerStr == "Foreground" || layerStr == "3") anim->layer = StoryboardLayer::Foreground;
        else if (layerStr == "Overlay" || layerStr == "4") anim->layer = StoryboardLayer::Overlay;

        // Origin
        std::string originStr = trim(parts[2]);
        if (originStr == "TopLeft" || originStr == "0") anim->origin = StoryboardOrigin::TopLeft;
        else if (originStr == "Centre" || originStr == "1") anim->origin = StoryboardOrigin::Centre;
        else if (originStr == "CentreLeft" || originStr == "2") anim->origin = StoryboardOrigin::CentreLeft;
        else if (originStr == "TopRight" || originStr == "3") anim->origin = StoryboardOrigin::TopRight;
        else if (originStr == "BottomCentre" || originStr == "4") anim->origin = StoryboardOrigin::BottomCentre;
        else if (originStr == "TopCentre" || originStr == "5") anim->origin = StoryboardOrigin::TopCentre;
        else if (originStr == "CentreRight" || originStr == "7") anim->origin = StoryboardOrigin::CentreRight;
        else if (originStr == "BottomLeft" || originStr == "8") anim->origin = StoryboardOrigin::BottomLeft;
        else if (originStr == "BottomRight" || originStr == "9") anim->origin = StoryboardOrigin::BottomRight;

        // Filepath
        std::string fn = trim(parts[3]);
        if (fn.size() >= 2 && fn.front() == '"' && fn.back() == '"') {
            fn = fn.substr(1, fn.size() - 2);
        }
        anim->filepath = fn;

        // Position
        anim->x = std::stof(parts[4]);
        anim->y = std::stof(parts[5]);

        // Animation specific
        anim->frameCount = std::stoi(parts[6]);
        anim->frameDelay = std::stof(parts[7]);
        if (parts.size() > 8) {
            std::string loopType = trim(parts[8]);
            anim->loopForever = (loopType == "LoopForever" || loopType == "0");
        }
    } catch (...) {
        return nullptr;
    }

    int layerIdx = static_cast<int>(anim->layer);
    StoryboardAnimation* ptr = anim.get();
    sprites[layerIdx].push_back(std::move(anim));
    return ptr;
}

StoryboardCommand Storyboard::parseCommandLine(const std::string& line) {
    StoryboardCommand cmd;
    std::string trimmed = trim(line);

    auto parts = split(trimmed, ',');
    if (parts.size() < 4) return cmd;

    try {
        char cmdType = trim(parts[0])[0];
        cmd.easing = static_cast<EasingType>(std::stoi(parts[1]));
        cmd.startTime = std::stoll(parts[2]);
        cmd.endTime = parts[3].empty() ? cmd.startTime : std::stoll(parts[3]);

        switch (cmdType) {
            case 'F':
                cmd.type = StoryboardCommandType::Fade;
                cmd.startValue[0] = std::stof(parts[4]);
                cmd.endValue[0] = (parts.size() > 5) ? std::stof(parts[5]) : cmd.startValue[0];
                break;

            case 'M':
                if (parts[0].size() > 1 && parts[0][1] == 'X') {
                    cmd.type = StoryboardCommandType::MoveX;
                    cmd.startValue[0] = std::stof(parts[4]);
                    cmd.endValue[0] = (parts.size() > 5) ? std::stof(parts[5]) : cmd.startValue[0];
                } else if (parts[0].size() > 1 && parts[0][1] == 'Y') {
                    cmd.type = StoryboardCommandType::MoveY;
                    cmd.startValue[0] = std::stof(parts[4]);
                    cmd.endValue[0] = (parts.size() > 5) ? std::stof(parts[5]) : cmd.startValue[0];
                } else {
                    cmd.type = StoryboardCommandType::Move;
                    cmd.startValue[0] = std::stof(parts[4]);
                    cmd.startValue[1] = std::stof(parts[5]);
                    cmd.endValue[0] = (parts.size() > 6) ? std::stof(parts[6]) : cmd.startValue[0];
                    cmd.endValue[1] = (parts.size() > 7) ? std::stof(parts[7]) : cmd.startValue[1];
                }
                break;

            case 'S':
                cmd.type = StoryboardCommandType::Scale;
                cmd.startValue[0] = std::stof(parts[4]);
                cmd.endValue[0] = (parts.size() > 5) ? std::stof(parts[5]) : cmd.startValue[0];
                break;

            case 'V':
                cmd.type = StoryboardCommandType::VectorScale;
                cmd.startValue[0] = std::stof(parts[4]);
                cmd.startValue[1] = std::stof(parts[5]);
                cmd.endValue[0] = (parts.size() > 6) ? std::stof(parts[6]) : cmd.startValue[0];
                cmd.endValue[1] = (parts.size() > 7) ? std::stof(parts[7]) : cmd.startValue[1];
                break;

            case 'R':
                cmd.type = StoryboardCommandType::Rotate;
                cmd.startValue[0] = std::stof(parts[4]);
                cmd.endValue[0] = (parts.size() > 5) ? std::stof(parts[5]) : cmd.startValue[0];
                break;

            case 'C':
                cmd.type = StoryboardCommandType::Colour;
                cmd.startValue[0] = std::stof(parts[4]);
                cmd.startValue[1] = std::stof(parts[5]);
                cmd.startValue[2] = std::stof(parts[6]);
                cmd.endValue[0] = (parts.size() > 7) ? std::stof(parts[7]) : cmd.startValue[0];
                cmd.endValue[1] = (parts.size() > 8) ? std::stof(parts[8]) : cmd.startValue[1];
                cmd.endValue[2] = (parts.size() > 9) ? std::stof(parts[9]) : cmd.startValue[2];
                break;

            case 'P':
                cmd.type = StoryboardCommandType::Parameter;
                if (parts.size() > 4) {
                    std::string param = trim(parts[4]);
                    if (param == "H") cmd.parameter = StoryboardParameter::FlipH;
                    else if (param == "V") cmd.parameter = StoryboardParameter::FlipV;
                    else if (param == "A") cmd.parameter = StoryboardParameter::Additive;
                }
                break;
        }
    } catch (...) {}

    return cmd;
}

void Storyboard::update(int64_t currentTime, bool isPassing) {
    for (int i = 0; i < 5; i++) {
        // Skip Fail layer if passing, skip Pass layer if failing
        if (i == 1 && isPassing) continue;
        if (i == 2 && !isPassing) continue;

        for (auto& sprite : sprites[i]) {
            sprite->update(currentTime);
        }
    }
}

void Storyboard::renderBackground(SDL_Renderer* renderer) {
    if (!backgroundTexture) return;

    // Use logical resolution (1280x720) instead of output size
    int windowW = 1280, windowH = 720;

    float texW, texH;
    SDL_GetTextureSize(backgroundTexture, &texW, &texH);

    // Scale to fill window while maintaining aspect ratio
    float scaleX = (float)windowW / texW;
    float scaleY = (float)windowH / texH;
    float scale = std::max(scaleX, scaleY);

    float destW = texW * scale;
    float destH = texH * scale;
    float destX = (windowW - destW) / 2 + backgroundX * scale;
    float destY = (windowH - destH) / 2 + backgroundY * scale;

    SDL_FRect destRect = {destX, destY, destW, destH};
    SDL_RenderTexture(renderer, backgroundTexture, nullptr, &destRect);
}

void Storyboard::render(SDL_Renderer* renderer, StoryboardLayer layer, bool isPassing) {
    int layerIdx = static_cast<int>(layer);

    // Skip Fail layer if passing, skip Pass layer if failing
    if (layerIdx == 1 && isPassing) return;
    if (layerIdx == 2 && !isPassing) return;

    // Use logical resolution (1280x720) instead of output size
    int windowW = 1280, windowH = 720;

    // osu! coordinate system: scale based on height, add widescreen margin
    float scale = (float)windowH / 480.0f;
    int widescreenMargin = std::max(0, (int)((windowW - windowH * 4.0f / 3.0f) / 2.0f));

    for (auto& sprite : sprites[layerIdx]) {
        if (!sprite->visible || !sprite->texture) continue;
        if (sprite->currentOpacity <= 0) continue;

        float texW, texH;
        SDL_GetTextureSize(sprite->texture, &texW, &texH);

        // Get origin offset
        float originX, originY;
        sprite->getOriginOffset(texW, texH, originX, originY);

        // Calculate destination rect with osu! coordinate system
        float destW = texW * sprite->currentScaleX * scale;
        float destH = texH * sprite->currentScaleY * scale;
        float destX = sprite->currentX * scale + widescreenMargin - originX * sprite->currentScaleX * scale;
        float destY = sprite->currentY * scale - originY * sprite->currentScaleY * scale;

        SDL_FRect destRect = {destX, destY, destW, destH};

        // Set color and alpha
        SDL_SetTextureColorMod(sprite->texture, sprite->currentR, sprite->currentG, sprite->currentB);
        SDL_SetTextureAlphaMod(sprite->texture, (uint8_t)(sprite->currentOpacity * 255));

        // Set blend mode
        if (sprite->additive) {
            SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_ADD);
        } else {
            SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_BLEND);
        }

        // Calculate flip
        SDL_FlipMode flip = SDL_FLIP_NONE;
        if (sprite->flipH) flip = (SDL_FlipMode)(flip | SDL_FLIP_HORIZONTAL);
        if (sprite->flipV) flip = (SDL_FlipMode)(flip | SDL_FLIP_VERTICAL);

        // Render with rotation
        SDL_FPoint center = {originX * sprite->currentScaleX * scale,
                            originY * sprite->currentScaleY * scale};
        SDL_RenderTextureRotated(renderer, sprite->texture, nullptr, &destRect,
                                 sprite->currentRotation * 180.0f / (float)M_PI, &center, flip);
    }
}
