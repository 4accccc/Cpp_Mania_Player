#pragma once
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <cmath>
#include <cstdint>

// Render layers
enum class StoryboardLayer {
    Background = 0,
    Fail = 1,
    Pass = 2,
    Foreground = 3,
    Overlay = 4
};

// Origin positions
enum class StoryboardOrigin {
    TopLeft = 0,
    Centre = 1,
    CentreLeft = 2,
    TopRight = 3,
    BottomCentre = 4,
    TopCentre = 5,
    Custom = 6,
    CentreRight = 7,
    BottomLeft = 8,
    BottomRight = 9
};

// Command types
enum class StoryboardCommandType {
    Fade,
    Move,
    MoveX,
    MoveY,
    Scale,
    VectorScale,
    Rotate,
    Colour,
    Parameter,
    Loop,
    Trigger
};

// Parameter types
enum class StoryboardParameter {
    FlipH,
    FlipV,
    Additive
};

// Easing types (35 types)
enum class EasingType {
    Linear = 0,
    EasingOut = 1,
    EasingIn = 2,
    QuadIn = 3,
    QuadOut = 4,
    QuadInOut = 5,
    CubicIn = 6,
    CubicOut = 7,
    CubicInOut = 8,
    QuartIn = 9,
    QuartOut = 10,
    QuartInOut = 11,
    QuintIn = 12,
    QuintOut = 13,
    QuintInOut = 14,
    SineIn = 15,
    SineOut = 16,
    SineInOut = 17,
    ExpoIn = 18,
    ExpoOut = 19,
    ExpoInOut = 20,
    CircIn = 21,
    CircOut = 22,
    CircInOut = 23,
    ElasticIn = 24,
    ElasticOut = 25,
    ElasticHalfOut = 26,
    ElasticQuarterOut = 27,
    ElasticInOut = 28,
    BackIn = 29,
    BackOut = 30,
    BackInOut = 31,
    BounceIn = 32,
    BounceOut = 33,
    BounceInOut = 34
};

// Easing function implementation
class Easing {
public:
    static float apply(EasingType type, float t);
private:
    static float bounceOut(float t);
    static float elasticOut(float t);
    static float elasticIn(float t);
};

// Command structure
struct StoryboardCommand {
    StoryboardCommandType type = StoryboardCommandType::Fade;
    EasingType easing = EasingType::Linear;
    int64_t startTime = 0;
    int64_t endTime = 0;
    float startValue[4] = {0, 0, 0, 0};
    float endValue[4] = {0, 0, 0, 0};
    StoryboardParameter parameter = StoryboardParameter::FlipH;
    int loopCount = 0;
    std::vector<StoryboardCommand> loopCommands;
    std::string triggerName;
};

// Sprite object
struct StoryboardSprite {
    std::string filepath;
    StoryboardLayer layer = StoryboardLayer::Background;
    StoryboardOrigin origin = StoryboardOrigin::Centre;
    float x = 320, y = 240;
    int64_t startTime = 0;
    int64_t endTime = 0;
    std::vector<StoryboardCommand> commands;

    // Runtime state
    SDL_Texture* texture = nullptr;
    bool visible = false;
    float currentX = 320, currentY = 240;
    float currentScaleX = 1, currentScaleY = 1;
    float currentRotation = 0;
    float currentOpacity = 1;
    uint8_t currentR = 255, currentG = 255, currentB = 255;
    bool flipH = false, flipV = false;
    bool additive = false;

    virtual ~StoryboardSprite() = default;
    virtual void update(int64_t currentTime);
    void getOriginOffset(float texW, float texH, float& ox, float& oy) const;
};

// Animation object
struct StoryboardAnimation : StoryboardSprite {
    int frameCount = 1;
    float frameDelay = 100;
    bool loopForever = true;
    int64_t animStartTime = 0;

    void update(int64_t currentTime) override;
    int getCurrentFrame(int64_t currentTime) const;
};

// Storyboard main class
class Storyboard {
public:
    Storyboard();
    ~Storyboard();

    bool loadFromOsu(const std::string& osuPath);
    bool loadFromOsb(const std::string& osbPath);
    void setBeatmapDirectory(const std::string& dir);
    void loadTextures(SDL_Renderer* renderer);
    void unloadTextures();
    void clear();

    void update(int64_t currentTime, bool isPassing);
    void renderBackground(SDL_Renderer* renderer);
    void render(SDL_Renderer* renderer, StoryboardLayer layer, bool isPassing);

    bool hasStoryboard() const;
    bool hasBackground() const;
    const std::string& getBackgroundImage() const { return backgroundImage; }
    void setBackgroundImage(const std::string& path) { backgroundImage = path; }

private:
    std::string beatmapDir;
    std::string backgroundImage;
    int backgroundX = 0, backgroundY = 0;
    SDL_Texture* backgroundTexture = nullptr;
    SDL_Renderer* sdlRenderer = nullptr;

    std::vector<std::unique_ptr<StoryboardSprite>> sprites[5];
    std::map<std::string, SDL_Texture*> textureCache;

    bool parseEvents(const std::string& filepath);
    StoryboardSprite* parseSprite(const std::string& line);
    StoryboardAnimation* parseAnimation(const std::string& line);
    bool parseCommand(const std::string& line, StoryboardSprite* sprite);
    StoryboardCommand parseCommandLine(const std::string& line);

    SDL_Texture* loadTexture(const std::string& name);
    std::string findImageFile(const std::string& baseName) const;
    static std::string trim(const std::string& str);
    static std::vector<std::string> split(const std::string& str, char delim);
};
