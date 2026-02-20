#include "Renderer.h"
#include "SkinManager.h"
#include <algorithm>
#include <cmath>

Renderer::Renderer() : window(nullptr), renderer(nullptr), font(nullptr),
                       windowWidth(1280), windowHeight(720), judgeLineY(620),
                       keyCount(4), laneWidth(100), hitErrorIndicatorPos(0.0f),
                       hitErrorTargetPos(0.0f), hitErrorAnimStartPos(0.0f),
                       hitErrorAnimStartTime(0), lastHitErrorTime(0),
                       stageStartX(0), stageWidth(0), skinHitPosition(620),
                       hpBarCurrentFrame(0), hpBarLastFrameTime(0), hpBarFrameCount(0) {
    updateLaneLayout();
    // Initialize lightingN hit times
    for (int i = 0; i < 18; i++) {
        lightingNHitTime[i] = INT64_MIN;  // Far in the past (avoid triggering during prepare time)
        keyReleaseTime[i] = 0;
        prevKeyDown[i] = false;
    }
}

Renderer::~Renderer() {
    if (font) TTF_CloseFont(font);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
}

bool Renderer::init() {
    window = SDL_CreateWindow("Mania Player", windowWidth, windowHeight, 0);
    if (!window) return false;

    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) return false;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Try fonts with CJK support
    font = TTF_OpenFont("Fonts/DroidSansFallback.ttf", 24);
    if (!font) {
        font = TTF_OpenFont("C:/Windows/Fonts/msyh.ttc", 24);
    }
    if (!font) {
        font = TTF_OpenFont("C:/Windows/Fonts/arial.ttf", 24);
    }
    if (!font) {
        return false;
    }
    return true;
}

void Renderer::setKeyCount(int count) {
    keyCount = count;
    updateLaneLayout();
}

void Renderer::resetHitErrorIndicator() {
    hitErrorIndicatorPos = 0.0f;
    hitErrorTargetPos = 0.0f;
    hitErrorAnimStartPos = 0.0f;
    hitErrorAnimStartTime = 0;
    lastHitErrorTime = 0;
    // Reset lightingN hit times to prevent ghost effects
    for (int i = 0; i < 18; i++) {
        lightingNHitTime[i] = INT64_MIN;
    }
}

void Renderer::resetKeyReleaseTime() {
    for (int i = 0; i < 18; i++) {
        keyReleaseTime[i] = 0;
        prevKeyDown[i] = false;
    }
}

void Renderer::updateLaneLayout() {
    // Calculate lane width based on key count
    // Max total width is 600 pixels for playfield
    int maxWidth = 600;
    laneWidth = std::min(100, maxWidth / keyCount);
    laneStartX = (windowWidth - keyCount * laneWidth) / 2;

    // Update skin-based layout
    updateSkinLayout();
}

void Renderer::updateSkinLayout() {
    columnWidths.clear();
    columnX.clear();

    const ManiaConfig* cfg = skinManager ? skinManager->getManiaConfig(keyCount) : nullptr;

    if (cfg && !cfg->columnWidth.empty()) {
        // Use skin config column widths
        // Note: cfg values are already scaled by POSITION_SCALE_FACTOR (1.6) from 480 to 768 coords
        float totalWidth = 0;
        for (int i = 0; i < keyCount; i++) {
            float w = (i < (int)cfg->columnWidth.size()) ? cfg->columnWidth[i] : ManiaConfig::DEFAULT_COLUMN_SIZE;
            columnWidths.push_back(w);
            totalWidth += w;
            // Add column spacing
            if (i < keyCount - 1 && i < (int)cfg->columnSpacing.size()) {
                totalWidth += cfg->columnSpacing[i];
            }
        }

        // Scale from 768 virtual coords to screen (values are pre-scaled from 480 to 768)
        float scale = (float)windowHeight / 768.0f;
        stageWidth = totalWidth * scale;
        stageStartX = (windowWidth - stageWidth) / 2.0f;

        // Calculate column X positions
        float x = stageStartX;
        for (int i = 0; i < keyCount; i++) {
            columnX.push_back(x);
            x += columnWidths[i] * scale;
            if (i < keyCount - 1 && i < (int)cfg->columnSpacing.size()) {
                x += cfg->columnSpacing[i] * scale;
            }
        }

        // Hit position from skin (already scaled to 768 coords)
        // hitPosition is distance from bottom of screen to judge line
        skinHitPosition = (float)windowHeight - cfg->hitPosition * scale;
        judgeLineY = (int)skinHitPosition;
    } else {
        // Fallback to default layout
        // Use 768 coords for columnWidths (consistent with skin layout)
        float scale768 = (float)windowHeight / 768.0f;
        float defaultWidth768 = ManiaConfig::DEFAULT_COLUMN_SIZE;  // 48.0f in 768 coords
        float defaultWidthScreen = defaultWidth768 * scale768;
        stageWidth = keyCount * defaultWidthScreen;
        stageStartX = (windowWidth - stageWidth) / 2.0f;

        for (int i = 0; i < keyCount; i++) {
            columnWidths.push_back(defaultWidth768);  // Store 768 coords
            columnX.push_back(stageStartX + i * defaultWidthScreen);
        }
        // hitPosition in 768 coords (480 - 402) * 1.6 = 124.8
        skinHitPosition = (float)windowHeight - ManiaConfig::DEFAULT_HIT_POSITION * scale768;
        judgeLineY = (int)skinHitPosition;
    }
}

float Renderer::getElementScale() const {
    // Scale for judgement and combo elements
    // Formula: Min(columnWidthScale, heightScale)
    // Condition: skinVersion >= 2.4 or heightScale < 1.0

    // Use 768 as base since skin values are pre-scaled to 768 coords
    float heightScale = (float)windowHeight / 768.0f;

    // Calculate column width scale (compare to default 48.0f which is 30*1.6)
    float totalColumnWidth = 0;
    for (int i = 0; i < keyCount; i++) {
        // getLaneWidth returns screen pixels, convert back to 768-coord width
        totalColumnWidth += getLaneWidth(i) / heightScale;
    }
    float columnWidthScale = totalColumnWidth / (ManiaConfig::DEFAULT_COLUMN_SIZE * keyCount);

    // Check skin version
    const ManiaConfig* cfg = skinManager ? skinManager->getManiaConfig(keyCount) : nullptr;
    float skinVersion = 1.0f;
    if (skinManager) {
        try {
            skinVersion = std::stof(skinManager->getConfig().version);
        } catch (...) {
            skinVersion = 2.5f;  // "latest" or invalid = assume new version
        }
    }

    // Apply scale if skin version >= 2.4 or height scale < 1.0
    float baseScale = 0.75f;  // Additional scale factor for better visual
    if (skinVersion >= 2.4f || heightScale < 1.0f) {
        return std::min(columnWidthScale, heightScale) * baseScale;
    }

    return baseScale;  // Apply base scale even without condition
}

void Renderer::clear() {
    SDL_SetRenderDrawColor(renderer, 20, 20, 30, 255);
    SDL_RenderClear(renderer);
}

void Renderer::present() {
    SDL_RenderPresent(renderer);
}

float Renderer::getLaneX(int lane) const {
    if (lane < (int)columnX.size()) {
        return columnX[lane];
    }
    return (float)(laneStartX + lane * laneWidth);
}

float Renderer::getLaneWidth(int lane) const {
    if (lane < (int)columnWidths.size()) {
        // columnWidths are pre-scaled to 768 coords
        float scale = (float)windowHeight / 768.0f;
        return columnWidths[lane] * scale;
    }
    return (float)laneWidth;
}

float Renderer::getColumnSpacing(int lane) const {
    const ManiaConfig* cfg = skinManager ? skinManager->getManiaConfig(keyCount) : nullptr;
    if (cfg && lane < (int)cfg->columnSpacing.size()) {
        // columnSpacing is pre-scaled to 768 coords
        float scale = (float)windowHeight / 768.0f;
        return cfg->columnSpacing[lane] * scale;
    }
    return 0.0f;
}

// Binary search to find the index of the last timing point at or before the given time
static int findTimingPointIndex(int64_t time, const std::vector<TimingPoint>& timingPoints) {
    if (timingPoints.empty()) return -1;

    int left = 0, right = (int)timingPoints.size() - 1;
    int result = -1;

    while (left <= right) {
        int mid = left + (right - left) / 2;
        if (timingPoints[mid].time <= time) {
            result = mid;
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    return result;
}

double Renderer::getSVMultiplier(int64_t time, const std::vector<TimingPoint>& timingPoints) const {
    int idx = findTimingPointIndex(time, timingPoints);
    if (idx < 0) return 1.0;

    // Search backwards from idx to find the nearest green line
    for (int i = idx; i >= 0; i--) {
        const auto& tp = timingPoints[i];
        if (!tp.uninherited && tp.beatLength < 0) {
            double sv = -tp.beatLength;
            // Allow very small SV (0.001) for Malody scroll=0 effect
            sv = std::max(0.1, std::min(10000.0, sv));
            return sv / 100.0;
        }
    }
    return 1.0;
}

double Renderer::getBaseBeatLength(int64_t time, const std::vector<TimingPoint>& timingPoints) const {
    int idx = findTimingPointIndex(time, timingPoints);
    if (idx < 0) return 500.0;  // default 120 BPM

    // Search backwards from idx to find the nearest red line
    for (int i = idx; i >= 0; i--) {
        const auto& tp = timingPoints[i];
        if (tp.uninherited && tp.beatLength > 0) {
            // Clamp to reasonable range to prevent overflow from extreme SV maps
            // Min: 1ms (60000 BPM), Max: 10000000ms (0.006 BPM) for extreme SV maps
            return std::clamp(tp.beatLength, 1.0, 10000000.0);
        }
    }
    return 500.0;
}

int Renderer::getNoteY(int64_t noteTime, int64_t currentTime, int scrollSpeed, double baseBPM, bool bpmScaleMode, const std::vector<TimingPoint>& timingPoints, bool ignoreSV, double clockRate) const {
    // osu!mania scroll speed formula from source code analysis:
    // distance = 21.0 * userSpeed * timeDiff / effectiveBeatLength
    //
    // osu! uses 640x480 virtual coordinates, scale to actual screen
    double scale = (double)windowHeight / 480.0;

    // Get base beat length (ms per beat)
    double baseBeatLength = 60000.0 / std::max(baseBPM, 1.0);

    // Calculate userSpeed based on speed mode
    double userSpeed;
    if (bpmScaleMode) {
        userSpeed = (double)scrollSpeed;
    } else {
        // Fixed mode: adjust by baseBPM and clockRate to compensate DT/HT
        userSpeed = (double)scrollSpeed * (100.0 / std::max(baseBPM * clockRate, 1.0));
    }

    // If ignoreSV, use simple calculation
    if (ignoreSV || timingPoints.empty()) {
        double timeDiff = noteTime - currentTime;
        double pixelOffset = 21.0 * userSpeed * timeDiff / baseBeatLength * scale;
        return judgeLineY - NOTE_HEIGHT - static_cast<int>(pixelOffset);
    }

    // With SV: cumulative pixel offset with SV changes
    double pixelOffset = 0.0;
    double t1 = currentTime;
    double t2 = noteTime;

    // Get current base beat length (from red line) - search backwards
    double currentBaseBL = baseBeatLength;
    int startIdx = findTimingPointIndex(t1, timingPoints);
    if (startIdx >= 0) {
        for (int i = startIdx; i >= 0; i--) {
            const auto& tp = timingPoints[i];
            if (tp.uninherited && tp.beatLength > 0) {
                // Clamp to reasonable range to prevent overflow from extreme SV maps
                currentBaseBL = std::clamp(tp.beatLength, 1.0, 10000000.0);
                break;
            }
        }
    }

    if (t1 >= t2) {
        // Note is in the past
        double sv = getSVMultiplier(t1, timingPoints);
        double effectiveBL = currentBaseBL * sv;
        double pixelOffset = 21.0 * userSpeed * (t2 - t1) / std::max(effectiveBL, 1.0) * scale;
        return judgeLineY - NOTE_HEIGHT - static_cast<int>(pixelOffset);
    }

    // Note is in the future - calculate cumulative distance with SV changes
    double currentSV = getSVMultiplier(t1, timingPoints);

    // Use binary search to find starting index for the loop
    int loopStartIdx = startIdx + 1;
    int endIdx = findTimingPointIndex(t2, timingPoints);

    for (int i = loopStartIdx; i <= endIdx && i < (int)timingPoints.size(); i++) {
        const auto& tp = timingPoints[i];
        if (tp.time < t1) continue;
        if (tp.time >= t2) break;

        double segmentTime = tp.time - t1;
        double effectiveBL = currentBaseBL * currentSV;
        pixelOffset += 21.0 * userSpeed * segmentTime / std::max(effectiveBL, 1.0) * scale;

        t1 = tp.time;
        if (tp.uninherited && tp.beatLength > 0) {
            // Clamp to reasonable range to prevent overflow from extreme SV maps
            currentBaseBL = std::clamp(tp.beatLength, 1.0, 10000000.0);
        } else if (!tp.uninherited && tp.beatLength < 0) {
            double sv = -tp.beatLength;
            // Allow very small SV (0.001) for Malody scroll=0 effect
            sv = std::max(0.1, std::min(10000.0, sv));
            currentSV = sv / 100.0;
        }
    }

    // Calculate remaining segment [t1, t2]
    double remainingTime = t2 - t1;
    double effectiveBL = currentBaseBL * currentSV;
    pixelOffset += 21.0 * userSpeed * remainingTime / std::max(effectiveBL, 1.0) * scale;

    return judgeLineY - NOTE_HEIGHT - static_cast<int>(pixelOffset);
}

int Renderer::getHoldHeadY(const Note& note, int naturalY, int64_t currentTime, int scrollSpeed, int releaseNaturalY) const {
    int y = naturalY;
    int judgeY = judgeLineY - NOTE_HEIGHT;

    if (!note.headHit) {
        return y;
    }

    if (note.headReleaseTime > 0) {
        if (note.headHitEarly && naturalY < judgeY) {
            // Early hit, head hasn't reached judge line yet - continue natural fall
            y = naturalY;
        } else {
            // Head was at judge line when released - fall from judge line at natural speed
            // Use offset from release moment to maintain consistent speed with tail
            y = judgeY + (naturalY - releaseNaturalY);
        }
    } else if (note.state == NoteState::Holding) {
        if (note.headHitEarly) {
            // Early hit: let head fall naturally but stop at judge line
            if (y > judgeY) {
                y = judgeY;
            }
        } else {
            // Late hit: pull head up to judge line
            y = judgeY;
        }
    }

    return y;
}

void Renderer::renderLanes() {
    const ManiaConfig* cfg = skinManager ? skinManager->getManiaConfig(keyCount) : nullptr;

    // Draw column backgrounds with skin colors
    for (int i = 0; i < keyCount; i++) {
        float x = (float)getLaneX(i);
        float w = getLaneWidth(i);

        // Get column color from skin config
        SDL_Color colColor = {0, 0, 0, 255};  // Default: pure black
        if (cfg && i < (int)cfg->colour.size()) {
            colColor = cfg->colour[i];
        }

        SDL_SetRenderDrawColor(renderer, colColor.r, colColor.g, colColor.b, colColor.a);
        SDL_FRect lane = { x, 0, w, (float)windowHeight };
        SDL_RenderFillRect(renderer, &lane);
    }

    // Draw column separator lines
    SDL_Color lineColor = {80, 80, 100, 255};  // Default
    if (cfg) {
        lineColor = cfg->colourColumnLine;
    }

    for (int i = 0; i <= keyCount; i++) {
        // Default line width: 2.0f if no skin at all, 0 if skin loaded
        float lineWidth = skinManager ? 0.0f : 2.0f;
        if (cfg && i < (int)cfg->columnLineWidth.size()) {
            lineWidth = cfg->columnLineWidth[i];
        }

        // Check if line is enabled
        bool lineEnabled = true;
        if (cfg && i < (int)cfg->columnLine.size()) {
            lineEnabled = cfg->columnLine[i];
        }

        // Skip if line is disabled or width is 0
        if (!lineEnabled || lineWidth <= 0) continue;

        float x;
        if (i < keyCount) {
            x = (float)getLaneX(i);
        } else {
            x = (float)getLaneX(keyCount - 1) + getLaneWidth(keyCount - 1);
        }

        SDL_SetRenderDrawColor(renderer, lineColor.r, lineColor.g, lineColor.b, lineColor.a);
        SDL_FRect line = { x - lineWidth / 2, 0, lineWidth, (float)windowHeight };
        SDL_RenderFillRect(renderer, &line);
    }
}

void Renderer::renderStageBottom() {
    SDL_Texture* bottomTex = skinManager ? skinManager->getStageBottomTexture() : nullptr;
    if (!bottomTex) return;

    float texW, texH;
    SDL_GetTextureSize(bottomTex, &texW, &texH);

    // Calculate stage width
    float totalWidth = 0;
    for (int i = 0; i < keyCount; i++) {
        totalWidth += getLaneWidth(i);
        if (i < keyCount - 1) {
            totalWidth += getColumnSpacing(i);
        }
    }

    float x = (float)getLaneX(0);
    float scale = (float)windowHeight / 480.0f;
    float bottomH = texH * scale;

    // Position at bottom of screen
    float y = (float)windowHeight - bottomH;

    SDL_FRect dst = { x, y, totalWidth, bottomH };
    SDL_RenderTexture(renderer, bottomTex, nullptr, &dst);
}

void Renderer::renderStageBorders() {
    float stageX = (float)getLaneX(0);
    float stageEndX = stageX;
    for (int i = 0; i < keyCount; i++) {
        stageEndX += getLaneWidth(i);
        if (i < keyCount - 1) stageEndX += getColumnSpacing(i);
    }

    // Left border - Anchor: TopLeft, Origin: TopRight
    SDL_Texture* leftTex = skinManager ? skinManager->getStageLeftTexture() : nullptr;
    if (leftTex) {
        float texW, texH;
        SDL_GetTextureSize(leftTex, &texW, &texH);
        // X: keep original width, divide by ScaleAdjust for @2x
        // Y: stretch to fill stage height
        float scaleAdjust = skinManager->getTextureScaleAdjust(leftTex);
        float borderW = texW / scaleAdjust;
        float borderH = (float)windowHeight;
        // Origin TopRight: right edge of texture aligns to stage left edge
        float x = stageX - borderW;
        SDL_FRect dst = { x, 0, borderW, borderH };
        SDL_RenderTexture(renderer, leftTex, nullptr, &dst);
    }

    // Right border - Anchor: TopRight, Origin: TopLeft
    SDL_Texture* rightTex = skinManager ? skinManager->getStageRightTexture() : nullptr;
    if (rightTex) {
        float texW, texH;
        SDL_GetTextureSize(rightTex, &texW, &texH);
        float scaleAdjust = skinManager->getTextureScaleAdjust(rightTex);
        float borderW = texW / scaleAdjust;
        float borderH = (float)windowHeight;
        // Origin TopLeft: left edge of texture aligns to stage right edge
        SDL_FRect dst = { stageEndX, 0, borderW, borderH };
        SDL_RenderTexture(renderer, rightTex, nullptr, &dst);
    }
}

void Renderer::renderKeys(const bool* laneKeyDown, int count, int64_t currentTime) {
    // Use 768 scale for skin values (pre-scaled from 480 to 768)
    float scale = (float)windowHeight / 768.0f;
    const ManiaConfig* cfg = skinManager ? skinManager->getManiaConfig(keyCount) : nullptr;

    // Get hit position (already scaled to 768 coords, default is DEFAULT_HIT_POSITION)
    float hitPos = cfg ? cfg->hitPosition : ManiaConfig::DEFAULT_HIT_POSITION;
    // KeyImage Y = hitPosition * scale (this is the vertical center, Origin=CentreLeft)
    float keyCenterY = hitPos * scale;

    for (int i = 0; i < count; i++) {
        float x = (float)getLaneX(i);
        float w = getLaneWidth(i);

        // Track key release time for 80ms hold effect
        if (prevKeyDown[i] && !laneKeyDown[i]) {
            // Key was just released
            keyReleaseTime[i] = currentTime;
        }
        prevKeyDown[i] = laneKeyDown[i];

        // Show pressed texture if key is down OR within 80ms after release
        bool showPressed = laneKeyDown[i] ||
            (keyReleaseTime[i] > 0 && currentTime - keyReleaseTime[i] < 80);

        SDL_Texture* keyTex = nullptr;
        if (showPressed) {
            keyTex = skinManager ? skinManager->getKeyDownTexture(i, keyCount) : nullptr;
        } else {
            keyTex = skinManager ? skinManager->getKeyTexture(i, keyCount) : nullptr;
        }

        if (keyTex) {
            float texW, texH;
            SDL_GetTextureSize(keyTex, &texW, &texH);
            // lazer: Width fills column, Height keeps texture original height (no scaling)
            // For @2x textures, we need to scale down by 2
            // But since we're stretching width to column width, we just use texH directly
            // scaled to screen coordinates
            float keyH = texH * scale;

            // KeyImage bottom aligned to screen bottom (Origin = BottomCentre in lazer)
            float y = (float)windowHeight - keyH;
            SDL_FRect dst = { x, y, w, keyH };
            SDL_RenderTexture(renderer, keyTex, nullptr, &dst);
        }
    }
}

void Renderer::renderHitLighting(const bool* laneKeyDown, int count) {
    SDL_Texture* lightTex = skinManager ? skinManager->getStageLightTexture() : nullptr;
    if (!lightTex) return;

    const ManiaConfig* cfg = skinManager ? skinManager->getManiaConfig(keyCount) : nullptr;
    // Use 768 scale for skin values (pre-scaled from 480 to 768)
    float scale = (float)windowHeight / 768.0f;

    // Get light position from skin config (already scaled to 768 coords)
    // lightPosition is distance from bottom of screen
    float lightY = (float)judgeLineY;
    if (cfg && cfg->lightPosition > 0) {
        lightY = (float)windowHeight - cfg->lightPosition * scale;
    }

    float texW, texH;
    SDL_GetTextureSize(lightTex, &texW, &texH);

    // Set additive blending for light effect
    SDL_SetTextureBlendMode(lightTex, SDL_BLENDMODE_ADD);

    for (int i = 0; i < count; i++) {
        if (!laneKeyDown[i]) continue;

        float x = (float)getLaneX(i);
        float w = getLaneWidth(i);

        // Get light color from skin
        SDL_Color lightColor = {255, 255, 255, 255};
        if (cfg && i < (int)cfg->colourLight.size()) {
            lightColor = cfg->colourLight[i];
        }

        SDL_SetTextureColorMod(lightTex, lightColor.r, lightColor.g, lightColor.b);

        // Calculate light dimensions
        float aspectRatio = texH / texW;
        float lightH = w * aspectRatio * 2;  // Make it taller

        // Position light with center at judge line
        float y = (float)judgeLineY - lightH / 2.0f;
        SDL_FRect dst = { x, y, w, lightH };
        SDL_RenderTexture(renderer, lightTex, nullptr, &dst);
    }

    // Reset blend mode
    SDL_SetTextureBlendMode(lightTex, SDL_BLENDMODE_BLEND);
}

void Renderer::triggerLightingN(int lane, int64_t time) {
    if (lane >= 0 && lane < 18) {
        lightingNHitTime[lane] = time;
    }
}

void Renderer::renderNoteLighting(const bool* laneHoldActive, int count, int64_t currentTime) {
    if (!skinManager) return;

    const ManiaConfig* cfg = skinManager->getManiaConfig(keyCount);
    float baseScale = (float)windowHeight / 768.0f;

    // Lighting position is at judge line (HitPosition)
    float lightY = (float)judgeLineY;

    for (int i = 0; i < count && i < keyCount; i++) {
        float x = getLaneX(i);
        float w = getLaneWidth(i);
        float centerX = x + w / 2.0f;

        // Get column width for scale calculation (in 480 coords, default 30)
        float columnWidth = 30.0f;
        if (cfg && i < (int)cfg->columnWidth.size()) {
            // columnWidth is stored scaled by 1.6, convert back to 480 coords
            columnWidth = cfg->columnWidth[i] / ManiaConfig::POSITION_SCALE_FACTOR;
        }

        // Get light color from skin
        SDL_Color lightColor = {255, 255, 255, 255};
        if (cfg && i < (int)cfg->colourLight.size()) {
            lightColor = cfg->colourLight[i];
        }

        // Check if hold is active (LightingL - loops)
        if (laneHoldActive && laneHoldActive[i]) {
            int frameCount = skinManager->getLightingLFrameCount(keyCount);
            if (frameCount > 0) {
                // Frame interval: max(170ms/frameCount, 16.67ms)
                float frameInterval = std::max(170.0f / frameCount, 16.67f);
                int frame = (int)(currentTime / frameInterval) % frameCount;
                if (frame < 0) frame = 0;

                SDL_Texture* tex = skinManager->getLightingLTexture(keyCount, frame);
                if (tex) {
                    float texW, texH;
                    SDL_GetTextureSize(tex, &texW, &texH);

                    // Apply @2x scale adjust
                    float scaleAdj = skinManager->getTextureScaleAdjust(tex);
                    texW /= scaleAdj;
                    texH /= scaleAdj;

                    // Calculate scale: (LightingLWidth or ColumnWidth) / 30
                    float lightingWidth = columnWidth;
                    if (cfg && i < (int)cfg->holdNoteLightWidth.size() && cfg->holdNoteLightWidth[i] > 0) {
                        lightingWidth = cfg->holdNoteLightWidth[i] / ManiaConfig::POSITION_SCALE_FACTOR;
                    }
                    float lightScale = (lightingWidth / 30.0f) * baseScale;

                    float lightW = texW * lightScale;
                    float lightH = texH * lightScale;

                    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
                    SDL_SetTextureColorMod(tex, 255, 255, 255);  // LightingN/L keeps original color

                    SDL_FRect dst = { centerX - lightW / 2, lightY - lightH / 2, lightW, lightH };
                    SDL_RenderTexture(renderer, tex, nullptr, &dst);
                    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                }
            }
        }
        // Check if LightingN animation is active (plays once with fade out)
        else {
            int64_t elapsed = currentTime - lightingNHitTime[i];
            if (elapsed >= 0 && elapsed < 200) {  // Extended to 200ms for fade out
                int frameCount = skinManager->getLightingNFrameCount(keyCount);
                if (frameCount > 0) {
                    // Frame interval: max(170ms/frameCount, 16.67ms)
                    float frameInterval = std::max(170.0f / frameCount, 16.67f);
                    int frame = (int)(elapsed / frameInterval);
                    if (frame >= frameCount) frame = frameCount - 1;

                    SDL_Texture* tex = skinManager->getLightingNTexture(keyCount, frame);
                    if (tex) {
                        float texW, texH;
                        SDL_GetTextureSize(tex, &texW, &texH);

                        // Apply @2x scale adjust
                        float scaleAdj = skinManager->getTextureScaleAdjust(tex);
                        texW /= scaleAdj;
                        texH /= scaleAdj;

                        // Calculate scale: (LightingNWidth or ColumnWidth) / 30
                        float lightingWidth = columnWidth;
                        if (cfg && i < (int)cfg->explosionWidth.size() && cfg->explosionWidth[i] > 0) {
                            lightingWidth = cfg->explosionWidth[i] / ManiaConfig::POSITION_SCALE_FACTOR;
                        }
                        float lightScale = (lightingWidth / 30.0f) * baseScale;

                        float lightW = texW * lightScale;
                        float lightH = texH * lightScale;

                        // Calculate alpha for fade out (starts at 80ms, completes at 200ms)
                        Uint8 alpha = 255;
                        if (elapsed >= 80) {
                            float fadeProgress = (float)(elapsed - 80) / 120.0f;  // 120ms fade duration
                            if (fadeProgress > 1.0f) fadeProgress = 1.0f;
                            alpha = (Uint8)(255 * (1.0f - fadeProgress));
                        }

                        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_ADD);
                        SDL_SetTextureAlphaMod(tex, alpha);
                        SDL_SetTextureColorMod(tex, 255, 255, 255);

                        SDL_FRect dst = { centerX - lightW / 2, lightY - lightH / 2, lightW, lightH };
                        SDL_RenderTexture(renderer, tex, nullptr, &dst);
                        SDL_SetTextureAlphaMod(tex, 255);  // Reset alpha
                        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                    }
                }
            }
        }
    }
}

void Renderer::renderLaneHighlights(const bool* laneKeyDown, int count, bool hiddenMod, bool fadeInMod, int combo) {
    // Calculate cover height (same as renderNotes)
    int osuCoverHeight = std::min(400, 160 + combo / 2);
    int coverHeight = osuCoverHeight * judgeLineY / 480;
    int gradientHeight = coverHeight * 3 / 8;

    int hiddenFadeStartY = judgeLineY - coverHeight;
    int hiddenFadeEndY = hiddenFadeStartY + gradientHeight;
    int fadeInFadeEndY = coverHeight;
    int fadeInFadeStartY = fadeInFadeEndY - gradientHeight;

    for (int i = 0; i < count; i++) {
        if (laneKeyDown[i]) {
            int startY = 0;
            int endY = judgeLineY;
            float x = (float)getLaneX(i);
            float w = getLaneWidth(i);

            // Clip to visible region
            if (hiddenMod) {
                endY = std::min(endY, hiddenFadeEndY);
            }
            if (fadeInMod) {
                startY = std::max(startY, fadeInFadeStartY);
            }

            for (int y = startY; y < endY; y++) {
                float progress = (float)(y) / judgeLineY;
                int baseAlpha = (int)(15 + progress * 60);

                // Apply fade in hidden/fadein gradient zone
                float modAlpha = 1.0f;
                if (hiddenMod && y > hiddenFadeStartY) {
                    modAlpha = 1.0f - (float)(y - hiddenFadeStartY) / gradientHeight;
                }
                if (fadeInMod && y < fadeInFadeEndY) {
                    modAlpha = (float)(y - fadeInFadeStartY) / gradientHeight;
                }

                int alpha = (int)(baseAlpha * modAlpha);
                if (alpha <= 0) continue;

                SDL_SetRenderDrawColor(renderer, 100, 100, 130, alpha);
                SDL_RenderLine(renderer, x, (float)y, x + w, (float)y);
            }
        }
    }
}

void Renderer::renderJudgeLine() {
    const ManiaConfig* cfg = skinManager ? skinManager->getManiaConfig(keyCount) : nullptr;

    // Check if judgement line should be shown
    bool showLine = true;
    if (cfg) {
        showLine = cfg->judgementLine;
    }

    // Calculate hit position Y (values are pre-scaled to 768 coords)
    // hitPosition is distance from bottom of screen to judge line
    float hitY = (float)judgeLineY;
    if (cfg) {
        float scale = (float)windowHeight / 768.0f;
        hitY = (float)windowHeight - cfg->hitPosition * scale;
    }

    // Try to use skin stage hint texture
    SDL_Texture* hintTex = skinManager ? skinManager->getStageHintTexture(keyCount) : nullptr;
    if (hintTex) {
        float texW, texH;
        SDL_GetTextureSize(hintTex, &texW, &texH);
        float scale = (float)windowHeight / 768.0f;

        // Render hint texture across all lanes
        float x = (float)getLaneX(0);
        float totalWidth = 0;
        for (int i = 0; i < keyCount; i++) {
            totalWidth += getLaneWidth(i);
            if (i < keyCount - 1) {
                totalWidth += getColumnSpacing(i);
            }
        }

        // Use original texture height scaled to screen, width matches stage
        float hintHeight = texH * scale;

        // Position at hit line (center aligned to hitY)
        float y = hitY - hintHeight / 2.0f;

        SDL_FRect dst = { x, y, totalWidth, hintHeight };
        SDL_RenderTexture(renderer, hintTex, nullptr, &dst);
    }

    // Draw judgement line if enabled
    if (showLine) {
        SDL_Color lineColor = {255, 255, 255, 230};
        if (cfg) {
            lineColor = cfg->colourJudgementLine;
            lineColor.a = 230;  // 90% opacity like osu!
        }

        float x1 = (float)getLaneX(0);
        float x2 = x1;
        for (int i = 0; i < keyCount; i++) {
            x2 += getLaneWidth(i);
            if (i < keyCount - 1) {
                x2 += getColumnSpacing(i);
            }
        }

        SDL_SetRenderDrawColor(renderer, lineColor.r, lineColor.g, lineColor.b, lineColor.a);
        SDL_FRect line = { x1, hitY - 1, x2 - x1, 2 };
        SDL_RenderFillRect(renderer, &line);
    }
}

SDL_Color Renderer::getNoteSDLColor(NoteColor color) {
    switch (color) {
        case NoteColor::White:  return {255, 255, 255, 255};
        case NoteColor::Blue:   return {100, 150, 255, 255};
        case NoteColor::Yellow: return {255, 255, 100, 255};
        case NoteColor::Pink:   return {255, 150, 200, 255};
        default:                return {255, 255, 255, 255};
    }
}

void Renderer::renderNotes(std::vector<Note>& notes, int64_t currentTime, int scrollSpeed, double baseBPM, bool bpmScaleMode, const std::vector<TimingPoint>& timingPoints, const NoteColor* colors, bool hiddenMod, bool fadeInMod, int combo, bool ignoreSV, double clockRate) {
    // osu!stable formula: coverHeight = Min(400, 160 + combo / 2) in 480px coordinate
    // Scale to our coordinate system (judgeLineY)
    int osuCoverHeight = std::min(400, 160 + combo / 2);
    int coverHeight = osuCoverHeight * judgeLineY / 480;

    // Gradient zone: coverHeight - coverHeight/1.6 ≈ 37.5% of cover height
    int gradientHeight = coverHeight * 3 / 8;

    // Hidden: covers notes near judge line (fade out as they approach)
    // FadeIn: covers notes far from judge line (fade in as they approach)
    int hiddenFadeStartY = judgeLineY - coverHeight;
    int hiddenFadeEndY = hiddenFadeStartY + gradientHeight;

    int fadeInFadeEndY = coverHeight;
    int fadeInFadeStartY = fadeInFadeEndY - gradientHeight;

    // Pre-calculate firstFakeEndTime and lastFakeEndTime to avoid O(n²) nested loops
    const int64_t groupThreshold = 2000;
    int64_t firstFakeEndTime = INT64_MAX;
    int64_t lastFakeEndTime = INT64_MIN;
    for (const auto& n : notes) {
        if (n.isFakeNote && n.fakeNoteShouldFix) {
            if (n.endTime < firstFakeEndTime) {
                firstFakeEndTime = n.endTime;
            }
        }
    }
    // Find last fake note in first group
    if (firstFakeEndTime != INT64_MAX) {
        for (const auto& n : notes) {
            if (n.isFakeNote && n.fakeNoteShouldFix &&
                n.endTime <= firstFakeEndTime + groupThreshold &&
                n.endTime > lastFakeEndTime) {
                lastFakeEndTime = n.endTime;
            }
        }
    }

    for (auto& note : notes) {
        // Fake notes (NaN time in SV maps) - only render tail
        bool isFakeNote = note.isFakeNote;

        if (note.state != NoteState::Waiting &&
            note.state != NoteState::Holding &&
            note.state != NoteState::Released &&
            note.state != NoteState::Missed) continue;

        // For fake notes: render only tail, disappear at endTime
        if (isFakeNote) {
            if (!note.isHold) continue;  // Fake notes must be hold notes

            // Disappear when currentTime >= endTime
            if (currentTime >= note.endTime) continue;

            // Time-based culling: skip if endTime is too far in the future
            int64_t timeDiff = note.endTime - currentTime;
            if (timeDiff > 5000) continue;  // Only render fake notes within 5 seconds

            int endY;

            // For fake notes that should be fixed (extreme SV after endTime)
            if (note.fakeNoteShouldFix) {
                // Only treat as "fixed" if this fake note is in the first group
                // (within 2000ms of the first fake note)
                bool isFirstGroup = (note.endTime <= firstFakeEndTime + groupThreshold);

                if (!isFirstGroup) {
                    // Not in first group, treat as normal fake note (fall through to else branch)
                    int endY = getNoteY(note.endTime, currentTime, scrollSpeed, baseBPM, bpmScaleMode, timingPoints, ignoreSV, clockRate);
                    if (endY < -NOTE_HEIGHT * 3 || endY > windowHeight) continue;

                    float laneW = getLaneWidth(note.lane);
                    float x = (float)getLaneX(note.lane);
                    float w = laneW;

                    SDL_Texture* tailTex = skinManager ? skinManager->getNoteTailTexture(note.lane, keyCount) : nullptr;
                    if (tailTex) {
                        float texW, texH;
                        SDL_GetTextureSize(tailTex, &texW, &texH);
                        float tailH = w * (texH / texW);
                        float tailY = (float)(endY + NOTE_HEIGHT) - tailH;
                        SDL_SetTextureAlphaMod(tailTex, 255);
                        SDL_FRect tail = { x, tailY, w, tailH };
                        SDL_RenderTextureRotated(renderer, tailTex, nullptr, &tail, 0, nullptr, SDL_FLIP_VERTICAL);
                    }
                    continue;
                }

                // First group: fixed position fake notes
                if (note.fakeNoteHasFixedY) {
                    endY = (int)note.fakeNoteFixedY;
                } else {
                    // All fake notes in first group appear together
                    const int64_t appearDuration = 500;
                    int64_t appearTime = firstFakeEndTime - appearDuration;
                    if (currentTime < appearTime) continue;

                    // Calculate Y position
                    float pixelsPerMs = 0.6f * scrollSpeed / 18.0f;
                    int64_t timeDiff = note.endTime - firstFakeEndTime;
                    endY = judgeLineY - NOTE_HEIGHT - 50 - (int)(timeDiff * pixelsPerMs);
                    if (endY < 20) endY = 20;

                    note.fakeNoteFixedY = (float)endY;
                    note.fakeNoteHasFixedY = true;
                    SDL_Log("Fixed fake note endTime=%lld (first=%lld, lastInGroup=%lld) at Y=%d",
                            (long long)note.endTime, (long long)firstFakeEndTime, (long long)lastFakeEndTime, endY);
                }

                // First group disappears together at last note's endTime
                int64_t lastFakeEndTime = firstFakeEndTime;
                for (const auto& n : notes) {
                    if (n.isFakeNote && n.fakeNoteShouldFix &&
                        n.endTime <= firstFakeEndTime + groupThreshold &&
                        n.endTime > lastFakeEndTime) {
                        lastFakeEndTime = n.endTime;
                    }
                }
                if (currentTime >= lastFakeEndTime) continue;
            } else {
                // Normal fake notes: calculate position normally
                endY = getNoteY(note.endTime, currentTime, scrollSpeed, baseBPM, bpmScaleMode, timingPoints, ignoreSV, clockRate);
            }

            // Skip if not visible
            if (endY < -NOTE_HEIGHT * 3 || endY > windowHeight) continue;

            // Render only the tail for fake notes
            float laneW = getLaneWidth(note.lane);
            float x = (float)getLaneX(note.lane);
            float w = laneW;

            SDL_Texture* tailTex = skinManager ? skinManager->getNoteTailTexture(note.lane, keyCount) : nullptr;
            if (tailTex) {
                float texW, texH;
                SDL_GetTextureSize(tailTex, &texW, &texH);
                float tailH = w * (texH / texW);
                float tailY = (float)(endY + NOTE_HEIGHT) - tailH;
                SDL_SetTextureAlphaMod(tailTex, 255);
                SDL_FRect tail = { x, tailY, w, tailH };
                SDL_RenderTextureRotated(renderer, tailTex, nullptr, &tail, 0, nullptr, SDL_FLIP_VERTICAL);
            }
            continue;  // Skip normal rendering for fake notes
        }

        int y = getNoteY(note.time, currentTime, scrollSpeed, baseBPM, bpmScaleMode, timingPoints, ignoreSV, clockRate);

        // Handle hold note head position based on hit timing
        if (note.isHold) {
            int releaseNaturalY = 0;
            if (note.headReleaseTime > 0) {
                releaseNaturalY = getNoteY(note.time, note.headReleaseTime, scrollSpeed, baseBPM, bpmScaleMode, timingPoints, ignoreSV, clockRate);
            }
            y = getHoldHeadY(note, y, currentTime, scrollSpeed, releaseNaturalY);
        }

        if (y > windowHeight) {
            // For hold notes, don't skip even if head is below screen - tail may still be visible
            if (!note.isHold) {
                continue;
            }
        }

        // Calculate alpha for Hidden/FadeIn mods based on Y position
        auto calcHiddenAlpha = [&](int posY) -> Uint8 {
            if (hiddenMod) {
                if (posY >= hiddenFadeEndY) return 0;
                if (posY > hiddenFadeStartY) {
                    float fadeProgress = (float)(posY - hiddenFadeStartY) / gradientHeight;
                    return (Uint8)(255 * (1.0f - fadeProgress));
                }
            }
            if (fadeInMod) {
                if (posY < fadeInFadeStartY) return 0;
                if (posY < fadeInFadeEndY) {
                    float fadeProgress = (float)(posY - fadeInFadeStartY) / gradientHeight;
                    return (Uint8)(255 * fadeProgress);
                }
            }
            return 255;
        };

        Uint8 alpha = calcHiddenAlpha(y);
        if (alpha == 0 && !note.isHold) continue;

        SDL_Color c = getNoteSDLColor(colors[note.lane]);

        // Texture color mod: white normally, gray when missed/released
        Uint8 texMod = 255;
        if (note.state == NoteState::Missed) {
            texMod = 128;  // Gray for missed notes
        } else if (note.headGrayStartTime > 0) {
            // 60ms linear transition from white (255) to gray (128)
            int64_t elapsed = currentTime - note.headGrayStartTime;
            if (elapsed >= 60) {
                texMod = 128;
            } else if (elapsed > 0) {
                float progress = elapsed / 60.0f;
                texMod = (Uint8)(255 - (255 - 128) * progress);
            }
        }

        float laneW = getLaneWidth(note.lane);
        float x = (float)getLaneX(note.lane);
        float w = laneW;

        if (note.isHold && note.endTime > note.time) {
            int endY = getNoteY(note.endTime, currentTime, scrollSpeed, baseBPM, bpmScaleMode, timingPoints, ignoreSV, clockRate);

            // Skip if both head and tail are off screen (above or below)
            if (endY < -NOTE_HEIGHT && y < -NOTE_HEIGHT) continue;
            if (y > windowHeight + 200 && endY > windowHeight + 200) continue;

            // Calculate tail alpha based on its position
            Uint8 tailAlpha = calcHiddenAlpha(endY);

            // Skip entire hold note if both head and tail are hidden
            if (alpha == 0 && tailAlpha == 0) continue;

            // Get head texture height to calculate body bottom position
            float headH = (float)NOTE_HEIGHT;
            int headFrameCount = skinManager ? skinManager->getNoteHeadFrameCount(note.lane, keyCount) : 0;
            int headFrame = 0;
            if (headFrameCount > 1) {
                float frameInterval = skinManager->getNoteFrameInterval(headFrameCount);
                headFrame = (int)(currentTime / frameInterval) % headFrameCount;
                if (headFrame < 0) headFrame = 0;
            }
            SDL_Texture* headTex = skinManager ? skinManager->getNoteHeadTexture(note.lane, keyCount, headFrame) : nullptr;
            if (headTex) {
                float texW, texH;
                SDL_GetTextureSize(headTex, &texW, &texH);
                headH = w * (texH / texW);
            }
            float headTop = y + NOTE_HEIGHT - headH;
            float headCenter = headTop + headH / 2;  // Head center position

            // Get tail texture height
            float tailH = (float)NOTE_HEIGHT;
            int tailFrameCount = skinManager ? skinManager->getNoteTailFrameCount(note.lane, keyCount) : 0;
            int tailFrame = 0;
            if (tailFrameCount > 1) {
                float frameInterval = skinManager->getNoteFrameInterval(tailFrameCount);
                tailFrame = (int)(currentTime / frameInterval) % tailFrameCount;
                if (tailFrame < 0) tailFrame = 0;
            }
            SDL_Texture* tailTex = skinManager ? skinManager->getNoteTailTexture(note.lane, keyCount, tailFrame) : nullptr;
            if (tailTex) {
                float texW, texH;
                SDL_GetTextureSize(tailTex, &texW, &texH);
                tailH = w * (texH / texW);
            }

            // Calculate full body range (may extend off-screen)
            int fullBodyTop = endY + NOTE_HEIGHT;  // Tail position
            int fullBodyBottom = (int)headCenter;  // Head center position
            int fullBodyHeight = fullBodyBottom - fullBodyTop;

            if (fullBodyHeight <= 0) {
                // Body has no height, skip body rendering
            } else if (isFakeNote) {
                // Fake notes: skip body rendering, only render tail
            } else {
                // Clamp to screen bounds for actual rendering
                int renderTop = std::max(0, fullBodyTop);
                int renderBottom = std::min(windowHeight, fullBodyBottom);

                // Apply Hidden/FadeIn hard clipping (fully invisible area)
                if (hiddenMod) {
                    renderBottom = std::min(renderBottom, hiddenFadeEndY);
                }
                if (fadeInMod) {
                    renderTop = std::max(renderTop, fadeInFadeStartY);
                }

                // Render body if any part is visible
                if (renderBottom > renderTop) {
                    int bodyFrameCount = skinManager ? skinManager->getNoteBodyFrameCount(note.lane, keyCount) : 0;
                    int bodyFrame = 0;
                    if (bodyFrameCount > 1) {
                        float frameInterval = skinManager->getNoteBodyFrameInterval();  // Fixed 30ms
                        bodyFrame = (int)(currentTime / frameInterval) % bodyFrameCount;
                        if (bodyFrame < 0) bodyFrame = 0;
                    }
                    SDL_Texture* bodyTex = skinManager ? skinManager->getNoteBodyTexture(note.lane, keyCount, bodyFrame) : nullptr;

                    // Optimization: Only use segmented rendering for Hidden/FadeIn gradient effect
                    // Without these mods, render the entire body in one draw call
                    if (!hiddenMod && !fadeInMod) {
                        // Fast path: single draw call for entire body
                        float bodyHeight = (float)(renderBottom - renderTop);
                        if (bodyTex) {
                            float texW, texH;
                            SDL_GetTextureSize(bodyTex, &texW, &texH);
                            SDL_SetTextureAlphaMod(bodyTex, 255);
                            SDL_SetTextureColorMod(bodyTex, texMod, texMod, texMod);
                            // Tile the texture vertically
                            float srcOffsetY = (float)(renderTop - fullBodyTop);
                            float remainingHeight = bodyHeight;
                            float currentY = (float)renderTop;
                            while (remainingHeight > 0) {
                                float srcY = fmod(srcOffsetY, texH);
                                float srcH = std::min(texH - srcY, remainingHeight);
                                SDL_FRect srcRect = { 0, srcY, texW, srcH };
                                SDL_FRect dstRect = { (float)x, currentY, (float)w, srcH };
                                SDL_RenderTexture(renderer, bodyTex, &srcRect, &dstRect);
                                currentY += srcH;
                                srcOffsetY += srcH;
                                remainingHeight -= srcH;
                            }
                        } else {
                            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 64);
                            SDL_FRect body = { (float)x, (float)renderTop, (float)w, bodyHeight };
                            SDL_RenderFillRect(renderer, &body);
                        }
                    } else {
                        // Slow path: segmented rendering for gradient effect
                        const int SEGMENT_HEIGHT = 16;  // Increased from 4 to reduce draw calls
                        for (int segY = renderTop; segY < renderBottom; segY += SEGMENT_HEIGHT) {
                            int segBottom = std::min(segY + SEGMENT_HEIGHT, renderBottom);
                            int segMidY = (segY + segBottom) / 2;
                            Uint8 segAlpha = calcHiddenAlpha(segMidY);
                            if (segAlpha == 0) continue;

                            float srcOffsetY = (float)(segY - fullBodyTop);
                            float segHeight = (float)(segBottom - segY);

                            if (bodyTex) {
                                float texW, texH;
                                SDL_GetTextureSize(bodyTex, &texW, &texH);
                                float srcY = fmod(srcOffsetY, texH);
                                float srcH = std::min(texH - srcY, segHeight);

                                SDL_SetTextureAlphaMod(bodyTex, segAlpha);
                                SDL_SetTextureColorMod(bodyTex, texMod, texMod, texMod);
                                SDL_FRect srcRect = { 0, srcY, texW, srcH };
                                SDL_FRect dstRect = { (float)x, (float)segY, (float)w, segHeight };
                                SDL_RenderTexture(renderer, bodyTex, &srcRect, &dstRect);
                            } else {
                                Uint8 bodyAlpha = (Uint8)(64 * segAlpha / 255);
                                SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, bodyAlpha);
                                SDL_FRect body = { (float)x, (float)segY, (float)w, segHeight };
                                SDL_RenderFillRect(renderer, &body);
                            }
                        }
                    }
                }
            }

            // Render tail with its own alpha
            // Tail disappears at headCenter (same as body), not at head position
            float tailBottom = (float)(endY + NOTE_HEIGHT);  // Tail bottom position
            if (endY >= -NOTE_HEIGHT * 3 && tailBottom <= headCenter && tailAlpha > 0) {
                if (tailTex) {
                    float tailY = tailBottom - tailH;  // Tail top position

                    // Clip tail if it extends beyond headCenter
                    float visibleTailH = tailH;
                    float srcY = 0;
                    if (tailY + tailH > headCenter) {
                        // Tail extends beyond headCenter, need to clip
                        visibleTailH = headCenter - tailY;
                        if (visibleTailH <= 0) {
                            // Tail completely hidden
                            goto skip_tail;
                        }
                        // For vertically flipped texture, clip from top of source
                        srcY = tailH - visibleTailH;
                    }

                    SDL_SetTextureAlphaMod(tailTex, tailAlpha);
                    SDL_SetTextureColorMod(tailTex, texMod, texMod, texMod);

                    float texW, texH;
                    SDL_GetTextureSize(tailTex, &texW, &texH);
                    // Source rect for clipping (accounting for vertical flip)
                    SDL_FRect srcRect = { 0, srcY * (texH / tailH), texW, visibleTailH * (texH / tailH) };
                    SDL_FRect dstRect = { (float)x, tailY, (float)w, visibleTailH };
                    SDL_RenderTextureRotated(renderer, tailTex, &srcRect, &dstRect, 0, nullptr, SDL_FLIP_VERTICAL);
                } else {
                    Uint8 tailBodyAlpha = (Uint8)(64 * tailAlpha / 255);
                    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, tailBodyAlpha);
                    float clipH = std::min((float)NOTE_HEIGHT, headCenter - (float)endY);
                    if (clipH > 0) {
                        SDL_FRect tail = { (float)x, (float)endY, (float)w, clipH };
                        SDL_RenderFillRect(renderer, &tail);
                    }
                }
            }
            skip_tail:;
        }

        // Skip head rendering for fake notes (only tail is rendered)
        if (isFakeNote) continue;

        if (y >= -NOTE_HEIGHT * 3 && y <= windowHeight) {
            // Try to use skin texture, fallback to solid color
            // For hold notes, use getNoteHeadTexture; for regular notes, use getNoteTexture
            bool isHoldNote = (note.endTime > note.time);
            SDL_Texture* noteTex = nullptr;
            if (skinManager) {
                // Calculate animation frame based on current time
                int frameCount = isHoldNote
                    ? skinManager->getNoteHeadFrameCount(note.lane, keyCount)
                    : skinManager->getNoteFrameCount(note.lane, keyCount);
                int frame = 0;
                if (frameCount > 1) {
                    float frameInterval = skinManager->getNoteFrameInterval(frameCount);
                    frame = (int)(currentTime / frameInterval) % frameCount;
                    if (frame < 0) frame = 0;  // Handle negative time
                }
                noteTex = isHoldNote
                    ? skinManager->getNoteHeadTexture(note.lane, keyCount, frame)
                    : skinManager->getNoteTexture(note.lane, keyCount, frame);
            }
            if (noteTex) {
                // Get texture size to maintain aspect ratio
                float texW, texH;
                SDL_GetTextureSize(noteTex, &texW, &texH);
                float aspectRatio = texH / texW;
                float noteH = w * aspectRatio;  // Height based on width and aspect ratio
                float noteY = y + NOTE_HEIGHT - noteH;  // Align bottom to original position

                SDL_SetTextureAlphaMod(noteTex, alpha);
                SDL_SetTextureColorMod(noteTex, texMod, texMod, texMod);  // Gray when missed
                SDL_FRect head = { (float)x, noteY, (float)w, noteH };
                SDL_RenderTexture(renderer, noteTex, nullptr, &head);
            } else {
                SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, alpha);
                SDL_FRect head = { (float)x, (float)y, (float)w, (float)NOTE_HEIGHT };
                SDL_RenderFillRect(renderer, &head);
            }
        }
    }
}

void Renderer::renderJudgement(const std::string& text) {
    if (!font || text.empty()) return;
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), text.length(), white);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    float x = (float)(windowWidth / 2 - surface->w / 2);
    float y = (float)(judgeLineY - 140);
    SDL_FRect dst = { x, y, (float)surface->w, (float)surface->h };
    SDL_RenderTexture(renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
    SDL_DestroySurface(surface);
}

void Renderer::renderHitJudgement(int judgement, int64_t elapsedMs) {
    if (!skinManager || elapsedMs < 0 || elapsedMs > 220) return;

    // Map judgement index to texture name
    // 0=300g, 1=300, 2=200, 3=100, 4=50, 5=miss
    const char* names[] = {"300g", "300", "200", "100", "50", "0"};
    if (judgement < 0 || judgement > 5) return;

    // Get texture frames for animation
    auto frames = skinManager->getHitTextureFrames(names[judgement]);
    if (frames.empty()) return;

    // Select frame based on elapsed time (50ms per frame = 20fps)
    int frameIndex = (int)(elapsedMs / 50) % (int)frames.size();
    SDL_Texture* tex = frames[frameIndex];
    if (!tex) return;

    float texW, texH;
    SDL_GetTextureSize(tex, &texW, &texH);

    // Apply @2x scale adjust
    float scaleAdj = skinManager->getTextureScaleAdjust(tex);
    texW /= scaleAdj;
    texH /= scaleAdj;

    // Center on stage
    float stageX = (float)getLaneX(0);
    float totalWidth = 0;
    for (int i = 0; i < keyCount; i++) {
        totalWidth += getLaneWidth(i);
        if (i < keyCount - 1) totalWidth += getColumnSpacing(i);
    }

    float baseScale = (float)windowHeight / 480.0f;
    bool isMiss = (judgement == 5);

    // Calculate animation parameters
    float alpha = 1.0f;
    float animScale = 1.0f;
    float rotation = 0.0f;

    // Fade in (0-20ms)
    if (elapsedMs < 20) {
        alpha = (float)elapsedMs / 20.0f;
    }
    // Fade out (180-220ms)
    else if (elapsedMs >= 180) {
        alpha = 1.0f - (float)(elapsedMs - 180) / 40.0f;
    }

    if (isMiss) {
        // Miss animation: scale 1.2->1.0 (0-100ms) + rotation
        if (elapsedMs < 100) {
            float t = (float)elapsedMs / 100.0f;
            animScale = 1.2f - 0.2f * t;
        }
        // Random rotation (use judgement time as seed for consistency)
        rotation = 0.05f;  // Small fixed rotation for miss
    } else {
        // Normal judgement animation
        if (elapsedMs < 40) {
            // Scale 0.8->1.0 (0-40ms)
            float t = (float)elapsedMs / 40.0f;
            animScale = 0.8f + 0.2f * t;
        } else if (elapsedMs < 80) {
            // Scale 1.0->0.7 (40-80ms)
            float t = (float)(elapsedMs - 40) / 40.0f;
            animScale = 1.0f - 0.3f * t;
        } else if (elapsedMs < 180) {
            // Hold at 0.7
            animScale = 0.7f;
        } else {
            // Scale 0.7->0.4 (180-220ms)
            float t = (float)(elapsedMs - 180) / 40.0f;
            animScale = 0.7f - 0.3f * t;
        }
    }

    // Remove elementScale, only use animation scale
    float finalScale = animScale;
    float w = texW * finalScale;
    float h = texH * finalScale;
    float centerX = stageX + totalWidth / 2;

    // Use skin config scorePosition or default (scorePosition is pre-scaled to 768 coords)
    const ManiaConfig* cfg = skinManager ? skinManager->getManiaConfig(keyCount) : nullptr;
    float skinScale = (float)windowHeight / 768.0f;
    float centerY = cfg ? cfg->scorePosition * skinScale : (float)judgeLineY - 120;

    SDL_SetTextureAlphaMod(tex, (Uint8)(alpha * 255));

    if (rotation != 0.0f) {
        // Render with rotation
        SDL_FRect dst = { centerX - w/2, centerY - h/2, w, h };
        SDL_RenderTextureRotated(renderer, tex, nullptr, &dst, rotation * 57.2958f, nullptr, SDL_FLIP_NONE);
    } else {
        SDL_FRect dst = { centerX - w/2, centerY - h/2, w, h };
        SDL_RenderTexture(renderer, tex, nullptr, &dst);
    }
}

void Renderer::renderSpeedInfo(int scrollSpeed, bool bpmScaleMode, bool autoPlay, bool autoPlayEnabled) {
    if (!font) return;
    char buf[64];
    const char* modeStr = bpmScaleMode ? "BPM" : "Fixed";
    if (autoPlay) {
        snprintf(buf, sizeof(buf), "Speed: %d (%s) [AUTO]", scrollSpeed, modeStr);
    } else if (autoPlayEnabled) {
        snprintf(buf, sizeof(buf), "Speed: %d (%s) [Tab: Toggle AutoPlay]", scrollSpeed, modeStr);
    } else {
        snprintf(buf, sizeof(buf), "Speed: %d (%s)", scrollSpeed, modeStr);
    }
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* surface = TTF_RenderText_Blended(font, buf, strlen(buf), white);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FRect dst = { 10, 10, (float)surface->w, (float)surface->h };
    SDL_RenderTexture(renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
    SDL_DestroySurface(surface);
}

void Renderer::renderCombo(int combo, int64_t comboAnimTime, bool comboBreak, int64_t breakAnimTime, int lastComboValue, bool holdActive, int64_t holdColorTime) {
    if (combo < 2 && !comboBreak) return;

    const ManiaConfig* cfg = skinManager ? skinManager->getManiaConfig(keyCount) : nullptr;
    // comboPosition is pre-scaled to 768 coords
    float skinScale = (float)windowHeight / 768.0f;
    float comboY = cfg ? cfg->comboPosition * skinScale : (judgeLineY - 100);

    // Calculate stage center
    float stageX = (float)getLaneX(0);
    float totalWidth = 0;
    for (int i = 0; i < keyCount; i++) {
        totalWidth += getLaneWidth(i);
        if (i < keyCount - 1) totalWidth += getColumnSpacing(i);
    }
    float centerX = stageX + totalWidth / 2;

    // Get colors from skin config
    SDL_Color colorHold = cfg ? cfg->colourHold : SDL_Color{255, 191, 51, 255};
    SDL_Color colorBreak = cfg ? cfg->colourBreak : SDL_Color{255, 0, 0, 255};
    SDL_Color colorNormal = {255, 255, 255, 255};

    // Calculate hold color transition (300ms)
    SDL_Color currentColor = colorNormal;
    if (holdColorTime < 300) {
        float t = (float)holdColorTime / 300.0f;
        if (holdActive) {
            // White -> Hold color
            currentColor.r = (uint8_t)(colorNormal.r + (colorHold.r - colorNormal.r) * t);
            currentColor.g = (uint8_t)(colorNormal.g + (colorHold.g - colorNormal.g) * t);
            currentColor.b = (uint8_t)(colorNormal.b + (colorHold.b - colorNormal.b) * t);
        } else {
            // Hold color -> White
            currentColor.r = (uint8_t)(colorHold.r + (colorNormal.r - colorHold.r) * t);
            currentColor.g = (uint8_t)(colorHold.g + (colorNormal.g - colorHold.g) * t);
            currentColor.b = (uint8_t)(colorHold.b + (colorNormal.b - colorHold.b) * t);
        }
    } else {
        currentColor = holdActive ? colorHold : colorNormal;
    }

    // Handle combo break animation (fade out + scale up, 200ms)
    if (comboBreak && breakAnimTime < 200 && lastComboValue >= 2) {
        float t = (float)breakAnimTime / 200.0f;
        float alpha = 0.8f * (1.0f - t);
        float breakScale = 1.0f + 3.0f * t;  // 1->4

        // Render break combo with red color
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", lastComboValue);

        bool useSkinDigits = skinManager && skinManager->hasComboSkin();
        if (useSkinDigits) {
            int len = (int)strlen(buf);
            float digitTotalW = 0;
            std::vector<SDL_Texture*> digitTextures;
            std::vector<float> digitScales;
            for (int i = 0; i < len; i++) {
                int digit = buf[i] - '0';
                SDL_Texture* tex = skinManager->getComboDigitTexture(digit);
                if (!tex) { useSkinDigits = false; break; }
                float texW, texH;
                SDL_GetTextureSize(tex, &texW, &texH);
                float scaleAdj = skinManager->getTextureScaleAdjust(tex);
                digitTextures.push_back(tex);
                digitScales.push_back(scaleAdj);
                digitTotalW += texW / scaleAdj;
            }
            if (useSkinDigits && !digitTextures.empty()) {
                float startX = centerX - (digitTotalW * breakScale) / 2;
                float curX = startX;
                for (size_t i = 0; i < digitTextures.size(); i++) {
                    SDL_Texture* tex = digitTextures[i];
                    float texW, texH;
                    SDL_GetTextureSize(tex, &texW, &texH);
                    float scaleAdj = digitScales[i];
                    float baseW = texW / scaleAdj;
                    float baseH = texH / scaleAdj;
                    float w = baseW * breakScale;
                    float h = baseH * breakScale;
                    float y = comboY - (h - baseH) / 2;
                    SDL_SetTextureColorMod(tex, colorBreak.r, colorBreak.g, colorBreak.b);
                    SDL_SetTextureAlphaMod(tex, (uint8_t)(alpha * 255));
                    SDL_FRect dst = { curX, y, w, h };
                    SDL_RenderTexture(renderer, tex, nullptr, &dst);
                    SDL_SetTextureColorMod(tex, 255, 255, 255);
                    SDL_SetTextureAlphaMod(tex, 255);
                    curX += w;
                }
            }
        }
        if (!useSkinDigits && font) {
            SDL_Color breakColor = {colorBreak.r, colorBreak.g, colorBreak.b, (uint8_t)(alpha * 255)};
            SDL_Surface* surface = TTF_RenderText_Blended(font, buf, strlen(buf), breakColor);
            if (surface) {
                SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
                float w = (float)surface->w * breakScale;
                float h = (float)surface->h * breakScale;
                float x = centerX - w / 2;
                float y = comboY - (h - surface->h) / 2;
                SDL_FRect dst = { x, y, w, h };
                SDL_RenderTexture(renderer, texture, nullptr, &dst);
                SDL_DestroyTexture(texture);
                SDL_DestroySurface(surface);
            }
        }
    }

    if (combo < 2) return;

    // Calculate Y scale animation (1.4->1.0 over 300ms)
    float scaleY = 1.0f;
    if (comboAnimTime < 300) {
        float t = (float)comboAnimTime / 300.0f;
        scaleY = 1.4f - 0.4f * t;
    }

    // Try to use skin digits
    bool useSkinDigits = skinManager && skinManager->hasComboSkin();

    if (useSkinDigits) {
        // Render using skin digit textures
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", combo);
        int len = (int)strlen(buf);

        // Calculate total width first
        float digitTotalW = 0;
        std::vector<SDL_Texture*> digitTextures;
        std::vector<float> digitWidths;
        std::vector<float> digitScales;

        for (int i = 0; i < len; i++) {
            int digit = buf[i] - '0';
            SDL_Texture* tex = skinManager->getComboDigitTexture(digit);
            if (!tex) {
                useSkinDigits = false;
                break;
            }
            float texW, texH;
            SDL_GetTextureSize(tex, &texW, &texH);
            float scaleAdj = skinManager->getTextureScaleAdjust(tex);
            digitTextures.push_back(tex);
            digitWidths.push_back(texW / scaleAdj);
            digitScales.push_back(scaleAdj);
            digitTotalW += texW / scaleAdj;
        }

        if (useSkinDigits && !digitTextures.empty()) {
            float startX = centerX - digitTotalW / 2;
            float curX = startX;

            for (size_t i = 0; i < digitTextures.size(); i++) {
                SDL_Texture* tex = digitTextures[i];
                float texW, texH;
                SDL_GetTextureSize(tex, &texW, &texH);
                float scaleAdj = digitScales[i];
                float baseW = texW / scaleAdj;
                float baseH = texH / scaleAdj;

                float w = baseW;
                float h = baseH * scaleY;
                float y = comboY - (h - baseH) / 2;  // Adjust for Y scale

                SDL_FRect dst = { curX, y, w, h };
                // Apply hold color modulation
                SDL_SetTextureColorMod(tex, currentColor.r, currentColor.g, currentColor.b);
                SDL_SetTextureAlphaMod(tex, currentColor.a);
                SDL_RenderTexture(renderer, tex, nullptr, &dst);
                SDL_SetTextureColorMod(tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(tex, 255);
                curX += w;
            }
            return;
        }
    }

    // Fallback to font rendering
    if (!font) return;

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", combo);
    SDL_Surface* surface = TTF_RenderText_Blended(font, buf, strlen(buf), currentColor);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);

    float w = (float)surface->w;
    float h = (float)surface->h * scaleY;
    float x = centerX - w / 2;
    float y = comboY - (h - surface->h) / 2;

    SDL_FRect dst = { x, y, w, h };
    SDL_RenderTexture(renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
    SDL_DestroySurface(surface);
}

void Renderer::renderHPBar(double hpPercent) {
    // HP range is 0-200, hpPercent is 0.0-1.0
    double currentHP = hpPercent * 200.0;
    float scale = (float)windowHeight / 480.0f;

    // Update animation frame
    int64_t currentTime = SDL_GetTicks();
    if (skinManager && hpBarFrameCount == 0) {
        hpBarFrameCount = skinManager->getScorebarColourFrameCount();
    }
    if (hpBarFrameCount > 1 && currentTime - hpBarLastFrameTime > 16) {
        hpBarCurrentFrame = (hpBarCurrentFrame + 1) % hpBarFrameCount;
        hpBarLastFrameTime = currentTime;
    }

    // Get skin textures
    SDL_Texture* bgTex = skinManager ? skinManager->getScorebarBgTexture() : nullptr;
    SDL_Texture* fillTex = skinManager ? skinManager->getScorebarColourTexture(hpBarCurrentFrame) : nullptr;


    // Bar position: right side of play area
    float barWidth = 30.0f * scale;
    float barHeight = (float)windowHeight - 100.0f * scale;
    float barX = stageStartX + stageWidth + 10.0f * scale;
    float barTop = 50.0f * scale;
    float fillHeight = barHeight * (float)hpPercent;

    if (bgTex && fillTex) {
        float bgTexW, bgTexH, fillTexW, fillTexH;
        SDL_GetTextureSize(bgTex, &bgTexW, &bgTexH);
        SDL_GetTextureSize(fillTex, &fillTexW, &fillTexH);

        // Apply @2x scale adjust
        float bgScale = skinManager->getTextureScaleAdjust(bgTex);
        float fillScale = skinManager->getTextureScaleAdjust(fillTex);
        float bgW = bgTexW / bgScale;
        float bgH = bgTexH / bgScale;
        float fillW = fillTexW / fillScale;
        float fillH = fillTexH / fillScale;

        // osu! mania: scale 0.7, extra Y scale = windowHeight/480
        const float MANIA_SCALE = 0.7f;
        float extraScale = (float)windowHeight / 480.0f;

        // Scaled size (before rotation)
        float scaledBgW = bgW * MANIA_SCALE;
        float scaledBgH = bgH * MANIA_SCALE * extraScale;

        // Position: right of stage, bottom at screen bottom
        float targetX = stageStartX + stageWidth + 1.0f * extraScale;
        float targetY = (float)windowHeight - scaledBgW;  // Bottom of screen

        // Calculate dstrect position for -90 degree rotation around center
        // After rotation: visual width = scaledBgH, visual height = scaledBgW
        float dstX = targetX - scaledBgW / 2 + scaledBgH / 2;
        float dstY = targetY - scaledBgH / 2 + scaledBgW / 2;
        SDL_FRect bgDst = { dstX, dstY, scaledBgW, scaledBgH };
        SDL_RenderTextureRotated(renderer, bgTex, nullptr, &bgDst, -90.0, nullptr, SDL_FLIP_NONE);

        // Fill: clip width based on HP, same rotation
        // After rotation, fill should be BOTTOM aligned with background
        if (hpPercent > 0) {
            // fillSrc uses original texture coordinates
            float clipTexW = fillTexW * (float)hpPercent;
            SDL_FRect fillSrc = { 0, 0, clipTexW, fillTexH };

            // Scaled size uses logical size (after @2x adjustment)
            float clipW = fillW * (float)hpPercent;
            float scaledFillW = clipW * MANIA_SCALE;
            float scaledFillH = fillH * MANIA_SCALE * extraScale;

            // X position: use scaledFillH (not scaledBgH) for correct rotation alignment
            // Offset: +2 right, -4 up
            float fillDstX = targetX - scaledFillW / 2 + scaledFillH / 2 + 16.0f;
            float fillDstY = (float)windowHeight - scaledFillH / 2 - scaledFillW / 2 - 3.0f;
            SDL_FRect fillDst = { fillDstX, fillDstY, scaledFillW, scaledFillH };

            SDL_RenderTextureRotated(renderer, fillTex, &fillSrc, &fillDst, -90.0, nullptr, SDL_FLIP_NONE);
        }
    } else {
        // Fallback: simple rectangle rendering
        // Background
        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 200);
        SDL_FRect bgRect = { barX - 2, barTop - 2, barWidth + 4, barHeight + 4 };
        SDL_RenderFillRect(renderer, &bgRect);

        // Fill bar from bottom to top
        if (fillHeight > 0) {
            // Only turn red when HP < 40
            if (currentHP < 40.0) {
                float t = static_cast<float>((40.0 - currentHP) / 40.0);
                SDL_SetRenderDrawColor(renderer, 255, static_cast<Uint8>(255 * (1 - t)), static_cast<Uint8>(255 * (1 - t)), 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            }
            SDL_FRect hpRect = { barX, barTop + barHeight - fillHeight, barWidth, fillHeight };
            SDL_RenderFillRect(renderer, &hpRect);
        }

        // Border
        SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
        SDL_FRect borderRect = { barX - 1, barTop - 1, barWidth + 2, barHeight + 2 };
        SDL_RenderRect(renderer, &borderRect);
    }
}

void Renderer::renderHPBarKi(double currentHP, float barX, float barY, float scale) {
    if (!skinManager) return;

    // Select Ki texture based on HP
    SDL_Texture* kiTex = nullptr;
    if (currentHP < 40.0) {
        kiTex = skinManager->getScorebarKiDanger2Texture();
    } else if (currentHP < 100.0) {
        kiTex = skinManager->getScorebarKiDangerTexture();
    } else {
        kiTex = skinManager->getScorebarKiTexture();
    }

    if (!kiTex) return;

    float kiTexW, kiTexH;
    SDL_GetTextureSize(kiTex, &kiTexW, &kiTexH);

    // Apply @2x scale adjust
    float kiScale = skinManager->getTextureScaleAdjust(kiTex);
    float kiW = kiTexW / kiScale;
    float kiH = kiTexH / kiScale;

    // Position Ki at the top of the HP bar (end of fill)
    float scaledW = kiW * scale;
    float scaledH = kiH * scale;
    float kiX = barX - scaledW / 2;
    float kiY = barY - scaledH;

    SDL_FRect kiDst = { kiX, kiY, scaledW, scaledH };
    SDL_RenderTexture(renderer, kiTex, nullptr, &kiDst);
}

void Renderer::renderScorePanel(const char* playerName, int score, double accuracy, int maxCombo) {
    if (!font) return;
    float x = 20;
    float y = 50;
    char buf[64];

    // Player name
    renderText(playerName, x, y);
    y += 30;

    // Score
    snprintf(buf, sizeof(buf), "%d", score);
    renderText(buf, x, y);
    y += 30;

    // Accuracy and max combo
    snprintf(buf, sizeof(buf), "%.2f%% | %dx", accuracy, maxCombo);
    renderText(buf, x, y);
}

void Renderer::renderFPS(int fps) {
    if (!font) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "FPS: %d", fps);
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* surface = TTF_RenderText_Blended(font, buf, strlen(buf), white);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FRect dst = { (float)(windowWidth - surface->w - 10), 10, (float)surface->w, (float)surface->h };
    SDL_RenderTexture(renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
    SDL_DestroySurface(surface);
}

void Renderer::renderGameInfo(int64_t currentTime, int64_t totalTime, const int* judgeCounts, double accuracy, int score) {
    if (!font) return;
    SDL_Color white = {255, 255, 255, 255};
    float x = (float)(windowWidth - 220);
    float y = 40;
    float lineHeight = 28;

    auto drawLine = [&](const char* text) {
        SDL_Surface* s = TTF_RenderText_Blended(font, text, strlen(text), white);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
        SDL_FRect dst = { x, y, (float)s->w, (float)s->h };
        SDL_RenderTexture(renderer, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_DestroySurface(s);
        y += lineHeight;
    };

    char buf[64];
    snprintf(buf, sizeof(buf), "Score: %d", score);
    drawLine(buf);

    int curMin = (int)(currentTime / 60000);
    int curSec = (int)((currentTime % 60000) / 1000);
    int totMin = (int)(totalTime / 60000);
    int totSec = (int)((totalTime % 60000) / 1000);
    if (currentTime < 0) { curMin = 0; curSec = 0; }
    snprintf(buf, sizeof(buf), "Time %d:%02d/%d:%02d", curMin, curSec, totMin, totSec);
    drawLine(buf);

    snprintf(buf, sizeof(buf), "300g: %d", judgeCounts[0]);
    drawLine(buf);
    snprintf(buf, sizeof(buf), "300: %d", judgeCounts[1]);
    drawLine(buf);
    snprintf(buf, sizeof(buf), "200: %d", judgeCounts[2]);
    drawLine(buf);
    snprintf(buf, sizeof(buf), "100: %d", judgeCounts[3]);
    drawLine(buf);
    snprintf(buf, sizeof(buf), "50: %d", judgeCounts[4]);
    drawLine(buf);
    snprintf(buf, sizeof(buf), "Miss: %d", judgeCounts[5]);
    drawLine(buf);

    snprintf(buf, sizeof(buf), "Acc: %.2f%%", accuracy);
    drawLine(buf);
}

void Renderer::renderHitErrorBar(const std::vector<HitError>& errors, int64_t currentTime,
                                 int64_t window300g, int64_t window300, int64_t window200,
                                 int64_t window100, int64_t window50, int64_t windowMiss,
                                 const bool* enabled, float scale) {
    // osu!mania style hit error bar

    // Calculate max enabled window for bar width
    int64_t maxWindow = windowMiss;
    if (enabled) {
        // Find the largest enabled window
        if (enabled[5]) maxWindow = windowMiss;
        else if (enabled[4]) maxWindow = window50;
        else if (enabled[3]) maxWindow = window100;
        else if (enabled[2]) maxWindow = window200;
        else if (enabled[1]) maxWindow = window300;
        else if (enabled[0]) maxWindow = window300g;
    }

    // Bar dimensions per osu! spec
    float barWidth = (float)maxWindow * scale;  // Max enabled window × scale
    float barHeight = 3.0f * scale;              // Base height
    float bgHeight = barHeight * 4.0f;           // Background height = base × 4
    float barX = (float)(windowWidth / 2) - barWidth / 2;
    float barY = (float)(windowHeight) - bgHeight;  // Screen bottom

    // Colors: 300g=White, 300=Blue, 200=Green, 100/50=Yellow, Miss=Red
    SDL_Color color300g = {255, 0, 127, 255};   // Rose - Marvelous
    SDL_Color color300 = {50, 188, 231, 255};    // Blue - Perfect
    SDL_Color color200 = {87, 227, 19, 255};     // Green - Great
    SDL_Color color100 = {218, 174, 70, 255};    // Yellow - Good/Bad
    SDL_Color colorMiss = {200, 50, 50, 255};    // Red - Miss

    // Draw background (60% alpha)
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 153);
    SDL_FRect barBg = { barX, barY, barWidth, bgHeight };
    SDL_RenderFillRect(renderer, &barBg);

    float halfWidth = barWidth / 2.0f;
    float centerX = barX + halfWidth;
    float zoneY = barY + (bgHeight - barHeight) / 2.0f;  // 居中显示区域

    // Draw window zones from outside to inside (only if enabled)
    // Miss zone (red) - enabled[5]
    if (!enabled || enabled[5]) {
        SDL_SetRenderDrawColor(renderer, colorMiss.r, colorMiss.g, colorMiss.b, 128);
        SDL_FRect missZone = { barX, zoneY, barWidth, barHeight };
        SDL_RenderFillRect(renderer, &missZone);
    }

    // 50 zone (yellow) - enabled[4]
    if (!enabled || enabled[4]) {
        float zone50Width = (float)window50 / (float)windowMiss * halfWidth;
        SDL_SetRenderDrawColor(renderer, color100.r, color100.g, color100.b, 128);
        SDL_FRect zone50 = { centerX - zone50Width, zoneY, zone50Width * 2.0f, barHeight };
        SDL_RenderFillRect(renderer, &zone50);
    }

    // 100 zone (yellow) - enabled[3]
    if (!enabled || enabled[3]) {
        float zone100Width = (float)window100 / (float)windowMiss * halfWidth;
        SDL_SetRenderDrawColor(renderer, color100.r, color100.g, color100.b, 128);
        SDL_FRect zone100 = { centerX - zone100Width, zoneY, zone100Width * 2.0f, barHeight };
        SDL_RenderFillRect(renderer, &zone100);
    }

    // 200 zone (green) - enabled[2]
    if (!enabled || enabled[2]) {
        float zone200Width = (float)window200 / (float)windowMiss * halfWidth;
        SDL_SetRenderDrawColor(renderer, color200.r, color200.g, color200.b, 128);
        SDL_FRect zone200 = { centerX - zone200Width, zoneY, zone200Width * 2.0f, barHeight };
        SDL_RenderFillRect(renderer, &zone200);
    }

    // 300 zone (blue) - enabled[1]
    if (!enabled || enabled[1]) {
        float zone300Width = (float)window300 / (float)windowMiss * halfWidth;
        SDL_SetRenderDrawColor(renderer, color300.r, color300.g, color300.b, 128);
        SDL_FRect zone300 = { centerX - zone300Width, zoneY, zone300Width * 2.0f, barHeight };
        SDL_RenderFillRect(renderer, &zone300);
    }

    // 300g zone (rose) - enabled[0]
    if (!enabled || enabled[0]) {
        float zone300gWidth = (float)window300g / (float)windowMiss * halfWidth;
        SDL_SetRenderDrawColor(renderer, color300g.r, color300g.g, color300g.b, 128);
        SDL_FRect zone300g = { centerX - zone300gWidth, zoneY, zone300gWidth * 2.0f, barHeight };
        SDL_RenderFillRect(renderer, &zone300g);
    }

    // Draw hit error ticks (additive blend, 40% alpha)
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
    for (const auto& err : errors) {
        int64_t age = currentTime - err.time;
        if (age > 10000) continue;

        float alpha = 102.0f * (1.0f - (float)age / 10000.0f);  // 40% base alpha
        if (alpha <= 0) continue;

        float offsetRatio = (float)err.offset / (float)windowMiss;
        offsetRatio = std::max(-1.0f, std::min(1.0f, offsetRatio));
        float xPos = centerX + offsetRatio * halfWidth;

        // Select color based on offset (respecting enabled state)
        int64_t absOff = std::abs(err.offset);
        Uint8 r, g, b;
        if (absOff <= window300g && (!enabled || enabled[0])) {
            r = color300g.r; g = color300g.g; b = color300g.b;
        } else if (absOff <= window300 && (!enabled || enabled[1])) {
            r = color300.r; g = color300.g; b = color300.b;
        } else if (absOff <= window200 && (!enabled || enabled[2])) {
            r = color200.r; g = color200.g; b = color200.b;
        } else if (absOff <= window100 && (!enabled || enabled[3])) {
            r = color100.r; g = color100.g; b = color100.b;
        } else if (absOff <= window50 && (!enabled || enabled[4])) {
            r = color100.r; g = color100.g; b = color100.b;
        } else {
            r = colorMiss.r; g = colorMiss.g; b = colorMiss.b;
        }

        // Draw tick
        float tickHeight = bgHeight + 4.0f;
        float tickY = barY - 2.0f;
        SDL_SetRenderDrawColor(renderer, r, g, b, (Uint8)alpha);
        SDL_FRect tick = { xPos - 1.5f, tickY, 3.0f, tickHeight };
        SDL_RenderFillRect(renderer, &tick);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Update indicator position using osu! style animation
    // Only update target when a new hit occurs, then use 800ms tween
    if (!errors.empty()) {
        // Get the most recent error (errors are pushed in chronological order)
        const auto& mostRecent = errors.back();
        int64_t mostRecentTime = mostRecent.time;
        float mostRecentOffset = (float)mostRecent.offset;

        // Check if there's a new hit (by comparing time, not size)
        if (mostRecentTime > lastHitErrorTime) {
            lastHitErrorTime = mostRecentTime;

            // Calculate new target position with osu! style smoothing (0.8/0.2)
            float newTargetPos = mostRecentOffset / (float)windowMiss * halfWidth;
            newTargetPos = std::max(-halfWidth, std::min(halfWidth, newTargetPos));

            // Apply osu! smoothing: float_0 = float_0 * 0.8f + num2 * 0.2f
            hitErrorTargetPos = hitErrorTargetPos * 0.8f + newTargetPos * 0.2f;

            // Start animation from current position to new target
            hitErrorAnimStartPos = hitErrorIndicatorPos;
            hitErrorAnimStartTime = currentTime;
        }

        // Calculate current position using 800ms tween animation
        int64_t animElapsed = currentTime - hitErrorAnimStartTime;
        const int64_t animDuration = 800; // 800ms like osu!

        if (animElapsed >= animDuration) {
            // Animation complete
            hitErrorIndicatorPos = hitErrorTargetPos;
        } else {
            // Ease out interpolation for smooth movement
            float t = (float)animElapsed / (float)animDuration;
            // Use ease-out quad: t * (2 - t)
            t = t * (2.0f - t);
            hitErrorIndicatorPos = hitErrorAnimStartPos + (hitErrorTargetPos - hitErrorAnimStartPos) * t;
        }
    }

    // Draw indicator triangle above the bar (moved higher to avoid being covered)
    float triangleSize = 8.0f;
    float triangleX = centerX + hitErrorIndicatorPos;
    float triangleY = barY - 20.0f;  // Moved up
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 220);
    // Draw filled triangle (pointing down)
    for (int i = 0; i < (int)triangleSize; i++) {
        float width = triangleSize - i;
        SDL_RenderLine(renderer, triangleX - width, triangleY + i, triangleX + width, triangleY + i);
    }

    // Draw center line (0ms) - white, on top of everything
    float centerLineHeight = bgHeight + 4.0f;
    float centerLineY = barY - 2.0f;
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 220);
    SDL_FRect centerLine = { centerX - 2.0f, centerLineY, 4.0f, centerLineHeight };
    SDL_RenderFillRect(renderer, &centerLine);
}

void Renderer::renderResult(const std::string& title, const std::string& creator,
                            const int* judgeCounts, double accuracy, int maxCombo, int score) {
    if (!font) return;
    SDL_Color white = {255, 255, 255, 255};
    float y = 80;
    float lineHeight = 35;

    auto drawText = [&](const char* text, float yPos) {
        SDL_Surface* s = TTF_RenderText_Blended(font, text, strlen(text), white);
        if (!s) return;
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
        float x = (float)(windowWidth / 2 - s->w / 2);
        SDL_FRect dst = { x, yPos, (float)s->w, (float)s->h };
        SDL_RenderTexture(renderer, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_DestroySurface(s);
    };

    drawText("== RESULT ==", y);
    y += lineHeight * 1.5f;

    char buf[256];
    snprintf(buf, sizeof(buf), "%s", title.c_str());
    drawText(buf, y);
    y += lineHeight;

    snprintf(buf, sizeof(buf), "Creator: %s", creator.c_str());
    drawText(buf, y);
    y += lineHeight * 1.5f;

    // Score
    snprintf(buf, sizeof(buf), "Score: %d", score);
    drawText(buf, y);
    y += lineHeight * 1.5f;

    const char* names[] = {"Marvelous!!", "Perfect!", "Great", "Good", "Bad", "Miss"};
    for (int i = 0; i < 6; i++) {
        snprintf(buf, sizeof(buf), "%s: %d", names[i], judgeCounts[i]);
        drawText(buf, y);
        y += lineHeight;
    }

    y += lineHeight;
    snprintf(buf, sizeof(buf), "Accuracy: %.2f%%", accuracy);
    drawText(buf, y);
    y += lineHeight;

    snprintf(buf, sizeof(buf), "Max Combo: %d", maxCombo);
    drawText(buf, y);
    y += lineHeight * 2;

    drawText("Press ESC to exit", y);
}

void Renderer::renderMenu() {
    if (!font) return;
    SDL_Color white = {255, 255, 255, 255};

    const char* title = "Mania Player";
    SDL_Surface* s = TTF_RenderText_Blended(font, title, strlen(title), white);
    if (s) {
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
        float x = (float)(windowWidth / 2 - s->w / 2);
        SDL_FRect dst = { x, 150, (float)s->w, (float)s->h };
        SDL_RenderTexture(renderer, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_DestroySurface(s);
    }

    const char* version = "Version 0.0.7";
    s = TTF_RenderText_Blended(font, version, strlen(version), white);
    if (s) {
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
        float x = (float)(windowWidth / 2 - s->w / 2);
        SDL_FRect dst = { x, 190, (float)s->w, (float)s->h };
        SDL_RenderTexture(renderer, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_DestroySurface(s);
    }
}

bool Renderer::renderButton(const char* text, float x, float y, float w, float h,
                            int mouseX, int mouseY, bool clicked) {
    bool hover = mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;

    if (hover) {
        SDL_SetRenderDrawColor(renderer, 80, 80, 120, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 50, 50, 70, 255);
    }
    SDL_FRect rect = { x, y, w, h };
    SDL_RenderFillRect(renderer, &rect);

    SDL_SetRenderDrawColor(renderer, 150, 150, 180, 255);
    SDL_RenderRect(renderer, &rect);

    if (font) {
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface* s = TTF_RenderText_Blended(font, text, strlen(text), white);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            float tx = x + (w - s->w) / 2;
            float ty = y + (h - s->h) / 2;
            SDL_FRect dst = { tx, ty, (float)s->w, (float)s->h };
            SDL_RenderTexture(renderer, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            SDL_DestroySurface(s);
        }
    }

    return hover && clicked;
}

bool Renderer::renderCheckbox(const char* label, bool checked, float x, float y,
                               int mouseX, int mouseY, bool clicked) {
    float boxSize = 20;
    bool hover = mouseX >= x && mouseX <= x + boxSize && mouseY >= y && mouseY <= y + boxSize;

    SDL_SetRenderDrawColor(renderer, 60, 60, 80, 255);
    SDL_FRect box = {x, y, boxSize, boxSize};
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 150, 150, 180, 255);
    SDL_RenderRect(renderer, &box);

    if (checked) {
        SDL_SetRenderDrawColor(renderer, 100, 200, 100, 255);
        SDL_FRect inner = {x + 4, y + 4, boxSize - 8, boxSize - 8};
        SDL_RenderFillRect(renderer, &inner);
    }

    if (font) {
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface* s = TTF_RenderText_Blended(font, label, strlen(label), white);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FRect dst = {x + boxSize + 8, y + (boxSize - s->h) / 2, (float)s->w, (float)s->h};
            SDL_RenderTexture(renderer, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            SDL_DestroySurface(s);
        }
    }
    return hover && clicked;
}

int Renderer::renderSliderWithValue(float x, float y, float w, int value, int minVal, int maxVal,
                           int mouseX, int mouseY, bool mouseDown) {
    float h = 20;
    float trackH = 6;
    float knobW = 12;

    SDL_SetRenderDrawColor(renderer, 40, 40, 60, 255);
    SDL_FRect track = {x, y + (h - trackH) / 2, w, trackH};
    SDL_RenderFillRect(renderer, &track);

    float ratio = (float)(value - minVal) / (maxVal - minVal);
    float knobX = x + ratio * (w - knobW);

    bool hover = mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
    int newValue = value;

    if (hover && mouseDown) {
        float newRatio = (mouseX - x) / w;
        if (newRatio < 0) newRatio = 0;
        if (newRatio > 1) newRatio = 1;
        newValue = minVal + (int)(newRatio * (maxVal - minVal) + 0.5f);
        knobX = x + newRatio * (w - knobW);
    }

    SDL_SetRenderDrawColor(renderer, 100, 150, 255, 255);
    SDL_FRect knob = {knobX, y, knobW, h};
    SDL_RenderFillRect(renderer, &knob);

    if (font) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", newValue);
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface* s = TTF_RenderText_Blended(font, buf, strlen(buf), white);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FRect dst = {x + w + 10, y, (float)s->w, (float)s->h};
            SDL_RenderTexture(renderer, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            SDL_DestroySurface(s);
        }
    }

    return newValue;
}

int Renderer::renderSliderWithFloatValue(float x, float y, float w, int value, int minVal, int maxVal,
                                          float divisor, int mouseX, int mouseY, bool mouseDown) {
    float h = 20;
    float trackH = 6;
    float knobW = 12;

    SDL_SetRenderDrawColor(renderer, 40, 40, 60, 255);
    SDL_FRect track = {x, y + (h - trackH) / 2, w, trackH};
    SDL_RenderFillRect(renderer, &track);

    float ratio = (float)(value - minVal) / (maxVal - minVal);
    float knobX = x + ratio * (w - knobW);

    bool hover = mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
    int newValue = value;

    if (hover && mouseDown) {
        float newRatio = (mouseX - x) / w;
        if (newRatio < 0) newRatio = 0;
        if (newRatio > 1) newRatio = 1;
        newValue = minVal + (int)(newRatio * (maxVal - minVal) + 0.5f);
        knobX = x + newRatio * (w - knobW);
    }

    SDL_SetRenderDrawColor(renderer, 100, 150, 255, 255);
    SDL_FRect knob = {knobX, y, knobW, h};
    SDL_RenderFillRect(renderer, &knob);

    if (font) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", newValue / divisor);
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface* s = TTF_RenderText_Blended(font, buf, strlen(buf), white);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FRect dst = {x + w + 10, y, (float)s->w, (float)s->h};
            SDL_RenderTexture(renderer, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            SDL_DestroySurface(s);
        }
    }

    return newValue;
}

void Renderer::renderCategoryButton(const char* text, float x, float y, float w, float h, bool selected) {
    if (selected) {
        SDL_SetRenderDrawColor(renderer, 80, 80, 120, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 50, 50, 70, 255);
    }
    SDL_FRect rect = {x, y, w, h};
    SDL_RenderFillRect(renderer, &rect);

    if (font) {
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface* s = TTF_RenderText_Blended(font, text, strlen(text), white);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            float tx = x + (w - s->w) / 2;
            float ty = y + (h - s->h) / 2;
            SDL_FRect dst = {tx, ty, (float)s->w, (float)s->h};
            SDL_RenderTexture(renderer, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            SDL_DestroySurface(s);
        }
    }
}

void Renderer::renderSettingsWindow(int mouseX, int mouseY) {
    float winW = 800, winH = 500;
    float winX = (windowWidth - winW) / 2;
    float winY = (windowHeight - winH) / 2;

    SDL_SetRenderDrawColor(renderer, 30, 30, 45, 240);
    SDL_FRect bg = {winX, winY, winW, winH};
    SDL_RenderFillRect(renderer, &bg);

    SDL_SetRenderDrawColor(renderer, 100, 100, 140, 255);
    SDL_RenderRect(renderer, &bg);

    if (font) {
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface* s = TTF_RenderText_Blended(font, "Settings", 8, white);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FRect dst = {winX + 20, winY + 15, (float)s->w, (float)s->h};
            SDL_RenderTexture(renderer, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            SDL_DestroySurface(s);
        }
    }
}

void Renderer::renderKeyBindingUI(SDL_Keycode* keys, int keyCount, int currentIndex) {
    if (!font) return;
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color yellow = {255, 255, 0, 255};

    for (int i = 0; i < keyCount; i++) {
        char keyName[32];
        const char* name = SDL_GetKeyName(keys[i]);
        snprintf(keyName, sizeof(keyName), "%s", name);

        SDL_Color c = (i == currentIndex) ? yellow : white;
        SDL_Surface* s = TTF_RenderText_Blended(font, keyName, strlen(keyName), c);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            float x = (float)(getLaneX(i) + laneWidth / 2 - s->w / 2);
            float y = (float)(judgeLineY - 80);
            SDL_FRect dst = {x, y, (float)s->w, (float)s->h};
            SDL_RenderTexture(renderer, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            SDL_DestroySurface(s);
        }

        if (i == currentIndex) {
            const char* arrow = "v";
            SDL_Surface* as = TTF_RenderText_Blended(font, arrow, 1, yellow);
            if (as) {
                SDL_Texture* at = SDL_CreateTextureFromSurface(renderer, as);
                float ax = (float)(getLaneX(i) + laneWidth / 2 - as->w / 2);
                float ay = (float)(judgeLineY - 110);
                SDL_FRect adst = {ax, ay, (float)as->w, (float)as->h};
                SDL_RenderTexture(renderer, at, nullptr, &adst);
                SDL_DestroyTexture(at);
                SDL_DestroySurface(as);
            }
        }
    }
}

void Renderer::renderText(const char* text, float x, float y) {
    if (!font) return;
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* s = TTF_RenderText_Blended(font, text, strlen(text), white);
    if (s) {
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
        SDL_FRect dst = {x, y, (float)s->w, (float)s->h};
        SDL_RenderTexture(renderer, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_DestroySurface(s);
    }
}

void Renderer::renderTextRight(const char* text, float rightX, float y) {
    if (!font) return;
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* s = TTF_RenderText_Blended(font, text, strlen(text), white);
    if (s) {
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
        // Right-align: x = rightX - textWidth
        SDL_FRect dst = {rightX - (float)s->w, y, (float)s->w, (float)s->h};
        SDL_RenderTexture(renderer, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_DestroySurface(s);
    }
}

void Renderer::renderTextClipped(const char* text, float x, float y, float maxWidth) {
    if (!font || !text || maxWidth <= 0) return;
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* s = TTF_RenderText_Blended(font, text, strlen(text), white);
    if (s) {
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
        float srcW = (float)s->w;
        float srcH = (float)s->h;
        float dstW = srcW > maxWidth ? maxWidth : srcW;
        SDL_FRect src = {0, 0, dstW, srcH};
        SDL_FRect dst = {x, y, dstW, srcH};
        SDL_RenderTexture(renderer, t, &src, &dst);
        SDL_DestroyTexture(t);
        SDL_DestroySurface(s);
    }
}

int Renderer::getTextWidth(const char* text) {
    if (!font || !text || !text[0]) return 0;
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* s = TTF_RenderText_Blended(font, text, strlen(text), white);
    if (s) {
        int w = s->w;
        SDL_DestroySurface(s);
        return w;
    }
    return 0;
}

void Renderer::renderLabel(const char* text, float x, float y) {
    if (!font) return;
    SDL_Color gray = {180, 180, 180, 255};
    SDL_Surface* s = TTF_RenderText_Blended(font, text, strlen(text), gray);
    if (s) {
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
        SDL_FRect dst = {x, y, (float)s->w, (float)s->h};
        SDL_RenderTexture(renderer, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_DestroySurface(s);
    }
}

int Renderer::renderDropdown(const char* label, const char** options, int optionCount, int selected,
                              float x, float y, float w, int mouseX, int mouseY, bool& clicked, bool& expanded) {
    float h = 30;
    int newSelected = selected;

    if (font && label) {
        renderLabel(label, x, y);
        y += 25;
    }

    bool hoverMain = mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
    SDL_SetRenderDrawColor(renderer, hoverMain ? 60 : 45, hoverMain ? 60 : 45, hoverMain ? 80 : 65, 255);
    SDL_FRect mainRect = {x, y, w, h};
    SDL_RenderFillRect(renderer, &mainRect);
    SDL_SetRenderDrawColor(renderer, 100, 100, 130, 255);
    SDL_RenderRect(renderer, &mainRect);

    if (font && selected >= 0 && selected < optionCount) {
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface* s = TTF_RenderText_Blended(font, options[selected], strlen(options[selected]), white);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FRect dst = {x + 8, y + (h - s->h) / 2, (float)s->w, (float)s->h};
            SDL_RenderTexture(renderer, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            SDL_DestroySurface(s);
        }
    }

    if (hoverMain && clicked) {
        expanded = !expanded;
        clicked = false;  // Consume click
    }

    if (expanded) {
        // Store overlay data for deferred rendering (drawn on top of all controls)
        pendingDropdown_.active = true;
        pendingDropdown_.options.clear();
        for (int i = 0; i < optionCount; i++) {
            pendingDropdown_.options.push_back(options[i]);
        }
        pendingDropdown_.optionCount = optionCount;
        pendingDropdown_.selected = selected;
        pendingDropdown_.x = x;
        pendingDropdown_.y = y;
        pendingDropdown_.w = w;
        pendingDropdown_.h = h;
        pendingDropdown_.mouseX = mouseX;
        pendingDropdown_.mouseY = mouseY;
        pendingDropdown_.expandedPtr = &expanded;
        pendingDropdown_.resultPtr = nullptr;  // Will be handled in overlay

        // Check clicks now to update selection (result applied in overlay)
        for (int i = 0; i < optionCount; i++) {
            float optY = y + h + i * h;
            bool hoverOpt = mouseX >= x && mouseX <= x + w && mouseY >= optY && mouseY <= optY + h;
            if (hoverOpt && clicked) {
                newSelected = i;
                expanded = false;
                pendingDropdown_.active = false;
            }
        }

        // Consume click when dropdown is expanded (prevent click-through)
        if (clicked) {
            // Click outside options - close dropdown
            expanded = false;
            pendingDropdown_.active = false;
            clicked = false;
        }
    }

    return newSelected;
}

void Renderer::renderDropdownOverlay() {
    if (!pendingDropdown_.active) return;

    float x = pendingDropdown_.x;
    float y = pendingDropdown_.y;
    float w = pendingDropdown_.w;
    float h = pendingDropdown_.h;
    int mouseX = pendingDropdown_.mouseX;
    int mouseY = pendingDropdown_.mouseY;

    for (int i = 0; i < pendingDropdown_.optionCount; i++) {
        float optY = y + h + i * h;
        bool hoverOpt = mouseX >= x && mouseX <= x + w && mouseY >= optY && mouseY <= optY + h;
        SDL_SetRenderDrawColor(renderer, hoverOpt ? 70 : 50, hoverOpt ? 70 : 50, hoverOpt ? 90 : 70, 255);
        SDL_FRect optRect = {x, optY, w, h};
        SDL_RenderFillRect(renderer, &optRect);

        if (font && i < (int)pendingDropdown_.options.size()) {
            SDL_Color white = {255, 255, 255, 255};
            const std::string& opt = pendingDropdown_.options[i];
            SDL_Surface* s = TTF_RenderText_Blended(font, opt.c_str(), opt.size(), white);
            if (s) {
                SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
                SDL_FRect dst = {x + 8, optY + (h - s->h) / 2, (float)s->w, (float)s->h};
                SDL_RenderTexture(renderer, t, nullptr, &dst);
                SDL_DestroyTexture(t);
                SDL_DestroySurface(s);
            }
        }
    }

    pendingDropdown_.active = false;
}

bool Renderer::renderRadioButton(const char* label, bool selected, float x, float y,
                                  int mouseX, int mouseY, bool clicked) {
    float size = 18;
    bool hover = mouseX >= x && mouseX <= x + size && mouseY >= y && mouseY <= y + size;

    SDL_SetRenderDrawColor(renderer, 60, 60, 80, 255);
    SDL_FRect box = {x, y, size, size};
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 120, 120, 150, 255);
    SDL_RenderRect(renderer, &box);

    if (selected) {
        SDL_SetRenderDrawColor(renderer, 100, 180, 255, 255);
        SDL_FRect inner = {x + 4, y + 4, size - 8, size - 8};
        SDL_RenderFillRect(renderer, &inner);
    }

    if (font) {
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface* s = TTF_RenderText_Blended(font, label, strlen(label), white);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FRect dst = {x + size + 8, y + (size - s->h) / 2, (float)s->w, (float)s->h};
            SDL_RenderTexture(renderer, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            SDL_DestroySurface(s);
        }
    }

    return hover && clicked;
}

bool Renderer::renderClickableLabel(const char* text, float x, float y, int mouseX, int mouseY, bool clicked) {
    if (!font) return false;
    SDL_Surface* s = TTF_RenderText_Blended(font, text, strlen(text), {200, 200, 255, 255});
    if (!s) return false;

    float w = (float)s->w;
    float h = (float)s->h;
    bool hover = mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
    SDL_DestroySurface(s);

    SDL_Color color = hover ? SDL_Color{255, 255, 100, 255} : SDL_Color{200, 200, 255, 255};
    s = TTF_RenderText_Blended(font, text, strlen(text), color);
    if (s) {
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
        SDL_FRect dst = {x, y, (float)s->w, (float)s->h};
        SDL_RenderTexture(renderer, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_DestroySurface(s);
    }

    return hover && clicked;
}

void Renderer::renderPopupWindow(float x, float y, float w, float h) {
    SDL_SetRenderDrawColor(renderer, 35, 35, 50, 250);
    SDL_FRect bg = {x, y, w, h};
    SDL_RenderFillRect(renderer, &bg);
    SDL_SetRenderDrawColor(renderer, 120, 120, 160, 255);
    SDL_RenderRect(renderer, &bg);
}

void Renderer::setResolution(int width, int height) {
    SDL_SetWindowSize(window, width, height);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    // Keep logical resolution at 1280x720 for consistent UI
    SDL_SetRenderLogicalPresentation(renderer, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX);
}

void Renderer::setVSync(bool enabled) {
    SDL_SetRenderVSync(renderer, enabled ? 1 : 0);
}

void Renderer::setBorderlessFullscreen(bool enabled) {
    if (enabled) {
        SDL_SetWindowFullscreen(window, true);
    } else {
        SDL_SetWindowFullscreen(window, false);
    }
}

void Renderer::setWindowTitle(const std::string& title) {
    SDL_SetWindowTitle(window, title.c_str());
}

void Renderer::renderPauseMenu(int selection, float alpha) {
    uint8_t a = static_cast<uint8_t>(alpha * 255);

    // Dark overlay
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<uint8_t>(180 * alpha));
    SDL_FRect overlay = {0, 0, (float)windowWidth, (float)windowHeight};
    SDL_RenderFillRect(renderer, &overlay);

    if (!font) return;

    const char* options[] = {"Resume", "Retry", "Exit"};
    float menuY = 280;
    float lineHeight = 60;

    // Title
    SDL_Color white = {255, 255, 255, a};
    SDL_Surface* titleSurf = TTF_RenderText_Blended(font, "PAUSED", 6, white);
    if (titleSurf) {
        SDL_Texture* titleTex = SDL_CreateTextureFromSurface(renderer, titleSurf);
        SDL_SetTextureAlphaMod(titleTex, a);
        float tx = (float)(windowWidth / 2 - titleSurf->w / 2);
        SDL_FRect dst = {tx, 200, (float)titleSurf->w, (float)titleSurf->h};
        SDL_RenderTexture(renderer, titleTex, nullptr, &dst);
        SDL_DestroyTexture(titleTex);
        SDL_DestroySurface(titleSurf);
    }

    // Menu options
    for (int i = 0; i < 3; i++) {
        SDL_Color color = (i == selection) ? SDL_Color{255, 255, 0, a} : white;
        const char* prefix = (i == selection) ? "> " : "  ";
        char buf[32];
        snprintf(buf, sizeof(buf), "%s%s", prefix, options[i]);

        SDL_Surface* s = TTF_RenderText_Blended(font, buf, strlen(buf), color);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_SetTextureAlphaMod(t, a);
            float x = (float)(windowWidth / 2 - s->w / 2);
            SDL_FRect dst = {x, menuY + i * lineHeight, (float)s->w, (float)s->h};
            SDL_RenderTexture(renderer, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            SDL_DestroySurface(s);
        }
    }

    // Hint
    SDL_Surface* hint = TTF_RenderText_Blended(font, "Up/Down to select, Enter to confirm", 36, white);
    if (hint) {
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, hint);
        float x = (float)(windowWidth / 2 - hint->w / 2);
        SDL_FRect dst = {x, 500, (float)hint->w, (float)hint->h};
        SDL_RenderTexture(renderer, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_DestroySurface(hint);
    }
}

void Renderer::renderPauseCountdown(float seconds, float alpha) {
    if (!font || seconds < 0) return;
    uint8_t a = static_cast<uint8_t>(alpha * 255);

    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", seconds);

    SDL_Color yellow = {255, 255, 0, a};
    SDL_Surface* surf = TTF_RenderText_Blended(font, buf, strlen(buf), yellow);
    if (!surf) return;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_SetTextureAlphaMod(tex, a);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);

    // Scale up 3x for visibility
    float scale = 3.0f;
    float w = surf->w * scale;
    float h = surf->h * scale;
    float x = (windowWidth - w) / 2.0f;
    float y = (windowHeight - h) / 2.0f;
    SDL_FRect dst = {x, y, w, h};
    SDL_RenderTexture(renderer, tex, nullptr, &dst);

    SDL_DestroyTexture(tex);
    SDL_DestroySurface(surf);
}

void Renderer::renderDeathMenu(int selection, float slowdown, float alpha) {
    uint8_t a = static_cast<uint8_t>(alpha * 255);

    // Dark red overlay with slowdown-based alpha
    Uint8 overlayAlpha = (Uint8)(180 * (1.0f - slowdown * 0.5f) * alpha);
    SDL_SetRenderDrawColor(renderer, 40, 0, 0, overlayAlpha);
    SDL_FRect overlay = {0, 0, (float)windowWidth, (float)windowHeight};
    SDL_RenderFillRect(renderer, &overlay);

    if (!font) return;

    // Title "You Died!"
    SDL_Color red = {255, 60, 60, a};
    SDL_Color white = {255, 255, 255, a};
    SDL_Surface* titleSurf = TTF_RenderText_Blended(font, "You Died!", 9, red);
    if (titleSurf) {
        SDL_Texture* titleTex = SDL_CreateTextureFromSurface(renderer, titleSurf);
        SDL_SetTextureAlphaMod(titleTex, a);
        float tx = (float)(windowWidth / 2 - titleSurf->w / 2);
        SDL_FRect dst = {tx, 180, (float)titleSurf->w, (float)titleSurf->h};
        SDL_RenderTexture(renderer, titleTex, nullptr, &dst);
        SDL_DestroyTexture(titleTex);
        SDL_DestroySurface(titleSurf);
    }

    // Menu options
    const char* options[] = {"Export Replay", "Retry", "Quit"};
    float menuY = 300;
    float lineHeight = 60;

    for (int i = 0; i < 3; i++) {
        SDL_Color color = (i == selection) ? SDL_Color{255, 255, 0, a} : white;
        const char* prefix = (i == selection) ? "> " : "  ";
        char buf[32];
        snprintf(buf, sizeof(buf), "%s%s", prefix, options[i]);

        SDL_Surface* s = TTF_RenderText_Blended(font, buf, strlen(buf), color);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_SetTextureAlphaMod(t, a);
            float x = (float)(windowWidth / 2 - s->w / 2);
            SDL_FRect dst = {x, menuY + i * lineHeight, (float)s->w, (float)s->h};
            SDL_RenderTexture(renderer, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            SDL_DestroySurface(s);
        }
    }

    // Hint
    SDL_Surface* hint = TTF_RenderText_Blended(font, "Up/Down to select, Enter to confirm", 36, white);
    if (hint) {
        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, hint);
        float x = (float)(windowWidth / 2 - hint->w / 2);
        SDL_FRect dst = {x, 520, (float)hint->w, (float)hint->h};
        SDL_RenderTexture(renderer, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        SDL_DestroySurface(hint);
    }
}

void Renderer::renderSkipPrompt() {
    if (!font) return;

    SDL_Color white = {255, 255, 255, 200};
    SDL_Surface* surf = TTF_RenderText_Blended(font, "Press Space to skip...", 22, white);
    if (surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        // Position at bottom-right corner with some padding
        float x = (float)(windowWidth - surf->w - 20);
        float y = (float)(windowHeight - surf->h - 20);
        SDL_FRect dst = {x, y, (float)surf->w, (float)surf->h};
        SDL_RenderTexture(renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
        SDL_DestroySurface(surf);
    }
}

bool Renderer::renderColorBox(NoteColor color, float x, float y, float size, int mouseX, int mouseY, bool clicked) {
    SDL_Color c = getNoteSDLColor(color);
    bool hover = mouseX >= x && mouseX <= x + size && mouseY >= y && mouseY <= y + size;

    // Draw color box
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
    SDL_FRect box = {x, y, size, size};
    SDL_RenderFillRect(renderer, &box);

    // Draw border
    SDL_SetRenderDrawColor(renderer, hover ? 255 : 150, hover ? 255 : 150, hover ? 255 : 180, 255);
    SDL_RenderRect(renderer, &box);

    return hover && clicked;
}

bool Renderer::renderTextInput(const char* label, std::string& text, float x, float y, float w,
                                int mouseX, int mouseY, bool clicked, bool& editing, int& cursorPos) {
    float h = 30;
    float labelOffset = 0;
    float padding = 8;
    float contentWidth = w - padding * 2;

    // Render label if provided
    if (label && font) {
        renderLabel(label, x, y);
        labelOffset = 25;
    }

    float inputY = y + labelOffset;
    bool hover = mouseX >= x && mouseX <= x + w && mouseY >= inputY && mouseY <= inputY + h;

    // Background
    SDL_SetRenderDrawColor(renderer, editing ? 60 : 45, editing ? 60 : 45, editing ? 80 : 65, 255);
    SDL_FRect inputRect = {x, inputY, w, h};
    SDL_RenderFillRect(renderer, &inputRect);

    // Border
    SDL_SetRenderDrawColor(renderer, editing ? 150 : 100, editing ? 150 : 100, editing ? 200 : 130, 255);
    SDL_RenderRect(renderer, &inputRect);

    // Text content with cursor and scrolling
    if (font) {
        SDL_Color white = {255, 255, 255, 255};

        if (text.empty() && !editing) {
            // Show placeholder
            SDL_Surface* s = TTF_RenderText_Blended(font, "...", 3, white);
            if (s) {
                SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
                SDL_FRect dst = {x + padding, inputY + (h - s->h) / 2, (float)s->w, (float)s->h};
                SDL_RenderTexture(renderer, t, nullptr, &dst);
                SDL_DestroyTexture(t);
                SDL_DestroySurface(s);
            }
        } else {
            // Calculate cursor position in pixels
            float cursorPixelX = 0;
            if (cursorPos > 0 && !text.empty()) {
                std::string beforeCursor = text.substr(0, cursorPos);
                SDL_Surface* s = TTF_RenderText_Blended(font, beforeCursor.c_str(), beforeCursor.length(), white);
                if (s) {
                    cursorPixelX = (float)s->w;
                    SDL_DestroySurface(s);
                }
            }

            // Calculate scroll offset to keep cursor visible
            static float scrollOffset = 0;
            if (editing) {
                if (cursorPixelX - scrollOffset > contentWidth - 10) {
                    scrollOffset = cursorPixelX - contentWidth + 10;
                }
                if (cursorPixelX - scrollOffset < 0) {
                    scrollOffset = cursorPixelX;
                }
            } else {
                scrollOffset = 0;
            }

            // Save current clip rect and set input box clip
            SDL_Rect prevClip;
            SDL_GetRenderClipRect(renderer, &prevClip);
            bool clipWasEnabled = SDL_RenderClipEnabled(renderer);

            SDL_Rect clipRect = {(int)(x + padding), (int)inputY, (int)contentWidth, (int)h};
            SDL_SetRenderClipRect(renderer, &clipRect);

            // Render full text
            if (!text.empty()) {
                SDL_Surface* s = TTF_RenderText_Blended(font, text.c_str(), text.length(), white);
                if (s) {
                    SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
                    SDL_FRect dst = {x + padding - scrollOffset, inputY + (h - s->h) / 2, (float)s->w, (float)s->h};
                    SDL_RenderTexture(renderer, t, nullptr, &dst);
                    SDL_DestroyTexture(t);
                    SDL_DestroySurface(s);
                }
            }

            // Render cursor if editing
            if (editing) {
                float cursorScreenX = x + padding + cursorPixelX - scrollOffset;
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                SDL_FRect cursorRect = {cursorScreenX, inputY + 5, 2, h - 10};
                SDL_RenderFillRect(renderer, &cursorRect);
            }

            // Restore previous clip rect
            if (clipWasEnabled) {
                SDL_SetRenderClipRect(renderer, &prevClip);
            } else {
                SDL_SetRenderClipRect(renderer, nullptr);
            }
        }
    }

    // Toggle editing on click
    if (clicked) {
        if (hover) {
            if (!editing) {
                cursorPos = (int)text.length();
            }
            editing = true;
        } else {
            editing = false;
        }
    }

    return editing;
}

void Renderer::renderAnalysisChart(const AnalysisResult& result, int chartType, float x, float y, float w, float h) {
    if (result.pressDistributions.empty()) return;

    // Colors for each key (HSV to RGB)
    auto getKeyColor = [](int keyIndex, int keyCount) -> SDL_Color {
        float hue = (float)keyIndex / keyCount;
        float r, g, b;
        int i = (int)(hue * 6);
        float f = hue * 6 - i;
        float q = 1 - f;
        switch (i % 6) {
            case 0: r = 1; g = f; b = 0; break;
            case 1: r = q; g = 1; b = 0; break;
            case 2: r = 0; g = 1; b = f; break;
            case 3: r = 0; g = q; b = 1; break;
            case 4: r = f; g = 0; b = 1; break;
            default: r = 1; g = 0; b = q; break;
        }
        return {(Uint8)(r * 255), (Uint8)(g * 255), (Uint8)(b * 255), 255};
    };

    int keyCount = result.keyCount;

    if (chartType == 0) {
        // Press Time Distribution chart
        // Find max values for scaling
        int maxTime = 160;  // Max display time (ms)
        int maxCount = 1;
        for (const auto& dist : result.pressDistributions) {
            for (size_t i = 0; i < dist.presscount.size() && i < (size_t)maxTime; i++) {
                maxCount = std::max(maxCount, dist.presscount[i]);
            }
        }

        // Draw grid lines and tick marks
        SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);  // Dark gray for grid

        // X-axis ticks (every 40ms)
        for (int t = 0; t <= maxTime; t += 40) {
            float px = x + (float)t / maxTime * w;
            // Grid line
            if (t > 0 && t < maxTime) {
                SDL_RenderLine(renderer, px, y, px, y + h);
            }
            // Tick mark
            SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
            SDL_RenderLine(renderer, px, y + h, px, y + h + 5);
            SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
        }

        // Y-axis ticks (4 divisions)
        int yDivisions = 4;
        for (int i = 0; i <= yDivisions; i++) {
            float py = y + h - (float)i / yDivisions * h;
            // Grid line
            if (i > 0 && i < yDivisions) {
                SDL_RenderLine(renderer, x, py, x + w, py);
            }
            // Tick mark
            SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
            SDL_RenderLine(renderer, x - 5, py, x, py);
            SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
        }

        // Draw axes
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        SDL_RenderLine(renderer, x, y + h, x + w, y + h);  // X axis
        SDL_RenderLine(renderer, x, y, x, y + h);          // Y axis

        // Draw axis labels
        char maxLabel[32];
        snprintf(maxLabel, sizeof(maxLabel), "%d", maxCount);
        renderText(maxLabel, x - 40, y);
        renderText("0", x - 15, y + h + 5);  // Only one label at origin
        renderText("160ms", x + w - 40, y + h + 5);

        // Draw axis names at bottom center
        char axisInfo[128];
        snprintf(axisInfo, sizeof(axisInfo), "Y-Axis: Count,  X-Axis: Press Time (ms)");
        renderText(axisInfo, x + w / 2 - 130, y + h + 25);

        // Draw lines for each key
        for (int k = 0; k < keyCount; k++) {
            if (k >= (int)result.pressDistributions.size()) break;
            const auto& dist = result.pressDistributions[k];
            SDL_Color color = getKeyColor(k, keyCount);
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);

            float prevX = x, prevY = y + h;
            for (size_t i = 0; i < dist.presscount.size() && i < (size_t)maxTime; i++) {
                float px = x + (float)i / maxTime * w;
                float py = y + h - (float)dist.presscount[i] / maxCount * h;
                if (i > 0) {
                    SDL_RenderLine(renderer, prevX, prevY, px, py);
                }
                prevX = px;
                prevY = py;
            }
        }

        // Draw legend in top-right corner
        float legendX = x + w - 80;
        float legendY = y + 5;
        for (int k = 0; k < keyCount; k++) {
            SDL_Color color = getKeyColor(k, keyCount);
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
            SDL_FRect colorBox = {legendX, legendY + k * 18, 12, 12};
            SDL_RenderFillRect(renderer, &colorBox);
            char label[16];
            snprintf(label, sizeof(label), "Key %d", k + 1);
            renderText(label, legendX + 16, legendY + k * 18 - 2);
        }
    }
    else if (chartType == 1) {
        // Realtime Press Time chart
        float maxTime = result.maxGameTime > 0 ? result.maxGameTime : 1;
        float maxPress = 160;

        // Draw grid lines and tick marks
        SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);  // Dark gray for grid

        // X-axis ticks (5 divisions based on maxTime)
        int xDivisions = 5;
        for (int i = 0; i <= xDivisions; i++) {
            float px = x + (float)i / xDivisions * w;
            // Grid line
            if (i > 0 && i < xDivisions) {
                SDL_RenderLine(renderer, px, y, px, y + h);
            }
            // Tick mark
            SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
            SDL_RenderLine(renderer, px, y + h, px, y + h + 5);
            SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
        }

        // Y-axis ticks (every 40ms)
        for (int t = 0; t <= (int)maxPress; t += 40) {
            float py = y + h - (float)t / maxPress * h;
            // Grid line
            if (t > 0 && t < (int)maxPress) {
                SDL_RenderLine(renderer, x, py, x + w, py);
            }
            // Tick mark
            SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
            SDL_RenderLine(renderer, x - 5, py, x, py);
            SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
        }

        // Draw axes
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        SDL_RenderLine(renderer, x, y + h, x + w, y + h);
        SDL_RenderLine(renderer, x, y, x, y + h);

        // Draw axis labels
        renderText("160ms", x - 45, y);
        renderText("0", x - 15, y + h + 5);  // Only one label at origin
        char maxTimeLabel[32];
        snprintf(maxTimeLabel, sizeof(maxTimeLabel), "%.0fs", maxTime);
        renderText(maxTimeLabel, x + w - 30, y + h + 5);

        // Draw axis names at bottom center
        char axisInfo[128];
        snprintf(axisInfo, sizeof(axisInfo), "Y-Axis: Press Time (ms),  X-Axis: Play Time (s)");
        renderText(axisInfo, x + w / 2 - 150, y + h + 25);

        // Draw points for each key
        for (int k = 0; k < keyCount; k++) {
            if (k >= (int)result.realtimePress.size()) break;
            const auto& points = result.realtimePress[k];
            SDL_Color color = getKeyColor(k, keyCount);
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 200);

            for (const auto& pt : points) {
                float px = x + pt.gameTime / maxTime * w;
                float py = y + h - std::min(pt.pressTime, maxPress) / maxPress * h;
                SDL_FRect dot = {px - 1, py - 1, 3, 3};
                SDL_RenderFillRect(renderer, &dot);
            }
        }

        // Draw legend in top-right corner
        float legendX = x + w - 80;
        float legendY = y + 5;
        for (int k = 0; k < keyCount; k++) {
            SDL_Color color = getKeyColor(k, keyCount);
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
            SDL_FRect colorBox = {legendX, legendY + k * 18, 12, 12};
            SDL_RenderFillRect(renderer, &colorBox);
            char label[16];
            snprintf(label, sizeof(label), "Key %d", k + 1);
            renderText(label, legendX + 16, legendY + k * 18 - 2);
        }
    }
}

bool Renderer::saveAnalysisChart(const AnalysisResult& result, int chartType, const std::string& path, int width, int height) {
    // Create a texture to render to
    SDL_Texture* target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                            SDL_TEXTUREACCESS_TARGET, width, height);
    if (!target) return false;

    // Set render target
    SDL_SetRenderTarget(renderer, target);

    // Clear with dark background
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    SDL_RenderClear(renderer);

    // Render chart
    float margin = 50;
    renderAnalysisChart(result, chartType, margin, margin, width - margin * 2, height - margin * 2);

    // Read pixels and save
    SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
    SDL_SetRenderTarget(renderer, nullptr);
    SDL_DestroyTexture(target);

    if (!surface) return false;

    bool success = SDL_SaveBMP(surface, path.c_str());
    SDL_DestroySurface(surface);
    return success;
}
