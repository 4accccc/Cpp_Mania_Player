#include "Renderer.h"
#include "SkinManager.h"
#include <algorithm>

Renderer::Renderer() : window(nullptr), renderer(nullptr), font(nullptr),
                       windowWidth(1280), windowHeight(720), judgeLineY(620),
                       keyCount(4), laneWidth(100), hitErrorIndicatorPos(0.0f),
                       hitErrorTargetPos(0.0f), hitErrorAnimStartPos(0.0f),
                       hitErrorAnimStartTime(0), lastHitErrorCount(0),
                       stageStartX(0), stageWidth(0), skinHitPosition(620),
                       hpBarCurrentFrame(0), hpBarLastFrameTime(0), hpBarFrameCount(0) {
    updateLaneLayout();
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
    lastHitErrorCount = 0;
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
        float totalWidth = 0;
        for (int i = 0; i < keyCount; i++) {
            float w = (i < (int)cfg->columnWidth.size()) ? cfg->columnWidth[i] : 30.0f;
            columnWidths.push_back(w);
            totalWidth += w;
            // Add column spacing
            if (i < keyCount - 1 && i < (int)cfg->columnSpacing.size()) {
                totalWidth += cfg->columnSpacing[i];
            }
        }

        // Scale to fit screen (osu uses 640x480 virtual coords)
        float scale = (float)windowHeight / 480.0f;
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

        // Hit position from skin (virtual Y coordinate, scaled to screen)
        // osu! clamps HitPosition to 240-480, default 402
        skinHitPosition = cfg->hitPosition * scale;
        judgeLineY = (int)skinHitPosition;
    } else {
        // Fallback to default layout
        float scale = (float)windowHeight / 480.0f;
        float defaultWidth = 30.0f * scale;
        stageWidth = keyCount * defaultWidth;
        stageStartX = (windowWidth - stageWidth) / 2.0f;

        for (int i = 0; i < keyCount; i++) {
            columnWidths.push_back(defaultWidth);
            columnX.push_back(stageStartX + i * defaultWidth);
        }
        skinHitPosition = 402.0f * scale;
        judgeLineY = (int)skinHitPosition;
    }
}

float Renderer::getElementScale() const {
    // Scale for judgement and combo elements
    // Formula: Min(columnWidthScale, heightScale)
    // Condition: skinVersion >= 2.4 or heightScale < 1.0

    float heightScale = (float)windowHeight / 480.0f;

    // Calculate column width scale
    float totalColumnWidth = 0;
    for (int i = 0; i < keyCount; i++) {
        totalColumnWidth += getLaneWidth(i) / heightScale;  // Get unscaled width
    }
    float columnWidthScale = totalColumnWidth / (30.0f * keyCount);

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
        float scale = (float)windowHeight / 480.0f;
        return columnWidths[lane] * scale;
    }
    return (float)laneWidth;
}

float Renderer::getColumnSpacing(int lane) const {
    const ManiaConfig* cfg = skinManager ? skinManager->getManiaConfig(keyCount) : nullptr;
    if (cfg && lane < (int)cfg->columnSpacing.size()) {
        float scale = (float)windowHeight / 480.0f;
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
            sv = std::max(10.0, std::min(10000.0, sv));
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
            // Min: 6ms (10000 BPM), Max: 60000ms (1 BPM)
            return std::clamp(tp.beatLength, 6.0, 60000.0);
        }
    }
    return 500.0;
}

int Renderer::getNoteY(int64_t noteTime, int64_t currentTime, int scrollSpeed, double baseBPM, bool bpmScaleMode, const std::vector<TimingPoint>& timingPoints, bool ignoreSV) const {
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
        // Fixed mode: adjust by baseBPM
        userSpeed = (double)scrollSpeed * (100.0 / std::max(baseBPM, 1.0));
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
                currentBaseBL = std::clamp(tp.beatLength, 6.0, 60000.0);
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
        if (tp.time <= t1) continue;
        if (tp.time >= t2) break;

        double segmentTime = tp.time - t1;
        double effectiveBL = currentBaseBL * currentSV;
        pixelOffset += 21.0 * userSpeed * segmentTime / std::max(effectiveBL, 1.0) * scale;

        t1 = tp.time;
        if (tp.uninherited && tp.beatLength > 0) {
            // Clamp to reasonable range to prevent overflow from extreme SV maps
            currentBaseBL = std::clamp(tp.beatLength, 6.0, 60000.0);
        } else if (!tp.uninherited && tp.beatLength < 0) {
            double sv = -tp.beatLength;
            sv = std::max(10.0, std::min(10000.0, sv));
            currentSV = sv / 100.0;
        }
    }

    // Calculate remaining segment [t1, t2]
    double remainingTime = t2 - t1;
    double effectiveBL = currentBaseBL * currentSV;
    pixelOffset += 21.0 * userSpeed * remainingTime / std::max(effectiveBL, 1.0) * scale;

    return judgeLineY - NOTE_HEIGHT - static_cast<int>(pixelOffset);
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
        float lineWidth = 2.0f;
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
    float scale = (float)windowHeight / 480.0f;
    float stageX = (float)getLaneX(0);
    float stageEndX = stageX;
    for (int i = 0; i < keyCount; i++) {
        stageEndX += getLaneWidth(i);
        if (i < keyCount - 1) stageEndX += getColumnSpacing(i);
    }

    // Left border
    SDL_Texture* leftTex = skinManager ? skinManager->getStageLeftTexture() : nullptr;
    if (leftTex) {
        float texW, texH;
        SDL_GetTextureSize(leftTex, &texW, &texH);
        float borderW = texW * scale;
        float x = stageX - borderW;
        SDL_FRect dst = { x, 0, borderW, (float)windowHeight };
        SDL_RenderTexture(renderer, leftTex, nullptr, &dst);
    }

    // Right border
    SDL_Texture* rightTex = skinManager ? skinManager->getStageRightTexture() : nullptr;
    if (rightTex) {
        float texW, texH;
        SDL_GetTextureSize(rightTex, &texW, &texH);
        float borderW = texW * scale;
        SDL_FRect dst = { stageEndX, 0, borderW, (float)windowHeight };
        SDL_RenderTexture(renderer, rightTex, nullptr, &dst);
    }
}

void Renderer::renderKeys(const bool* laneKeyDown, int count) {
    float scale = (float)windowHeight / 480.0f;
    const ManiaConfig* cfg = skinManager ? skinManager->getManiaConfig(keyCount) : nullptr;

    // Get hit position (default 402)
    float hitPos = cfg ? (float)cfg->hitPosition : 402.0f;
    // KeyImage Y = hitPosition * scale (this is the vertical center, Origin=CentreLeft)
    float keyCenterY = hitPos * scale;

    for (int i = 0; i < count; i++) {
        float x = (float)getLaneX(i);
        float w = getLaneWidth(i);

        SDL_Texture* keyTex = nullptr;
        if (laneKeyDown[i]) {
            keyTex = skinManager ? skinManager->getKeyDownTexture(i, keyCount) : nullptr;
        } else {
            keyTex = skinManager ? skinManager->getKeyTexture(i, keyCount) : nullptr;
        }

        if (keyTex) {
            float texW, texH;
            SDL_GetTextureSize(keyTex, &texW, &texH);
            float keyH = w * (texH / texW);  // Keep aspect ratio

            // KeyImage bottom aligned to screen bottom
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
    float scale = (float)windowHeight / 480.0f;

    // Get light position from skin config
    float lightY = (float)judgeLineY;
    if (cfg && cfg->lightPosition > 0) {
        lightY = cfg->lightPosition * scale;
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

    // Calculate hit position Y
    float hitY = (float)judgeLineY;
    if (cfg) {
        float scale = (float)windowHeight / 480.0f;
        hitY = cfg->hitPosition * scale;
    }

    // Try to use skin stage hint texture
    SDL_Texture* hintTex = skinManager ? skinManager->getStageHintTexture(keyCount) : nullptr;
    if (hintTex) {
        float texW, texH;
        SDL_GetTextureSize(hintTex, &texW, &texH);
        float scale = (float)windowHeight / 480.0f;

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

void Renderer::renderNotes(std::vector<Note>& notes, int64_t currentTime, int scrollSpeed, double baseBPM, bool bpmScaleMode, const std::vector<TimingPoint>& timingPoints, const NoteColor* colors, bool hiddenMod, bool fadeInMod, int combo, bool ignoreSV) {
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
                // Find the first shouldFix fake note's endTime as reference
                int64_t firstFakeEndTime = INT64_MAX;
                for (const auto& n : notes) {
                    if (n.isFakeNote && n.fakeNoteShouldFix && n.endTime < firstFakeEndTime) {
                        firstFakeEndTime = n.endTime;
                    }
                }

                // Only treat as "fixed" if this fake note is in the first group
                // (within 2000ms of the first fake note)
                const int64_t groupThreshold = 2000;
                bool isFirstGroup = (note.endTime <= firstFakeEndTime + groupThreshold);

                if (!isFirstGroup) {
                    // Not in first group, treat as normal fake note (fall through to else branch)
                    int endY = getNoteY(note.endTime, currentTime, scrollSpeed, baseBPM, bpmScaleMode, timingPoints, ignoreSV);
                    if (endY < -NOTE_HEIGHT * 3 || endY > windowHeight) continue;

                    float laneW = getLaneWidth(note.lane);
                    float x = (float)getLaneX(note.lane) + 2;
                    float w = laneW - 4;

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
                    // Find last fake note in first group
                    int64_t lastFakeEndTime = firstFakeEndTime;
                    for (const auto& n : notes) {
                        if (n.isFakeNote && n.fakeNoteShouldFix &&
                            n.endTime <= firstFakeEndTime + groupThreshold &&
                            n.endTime > lastFakeEndTime) {
                            lastFakeEndTime = n.endTime;
                        }
                    }

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
                endY = getNoteY(note.endTime, currentTime, scrollSpeed, baseBPM, bpmScaleMode, timingPoints, ignoreSV);
            }

            // Skip if not visible
            if (endY < -NOTE_HEIGHT * 3 || endY > windowHeight) continue;

            // Render only the tail for fake notes
            float laneW = getLaneWidth(note.lane);
            float x = (float)getLaneX(note.lane) + 2;
            float w = laneW - 4;

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

        int y = getNoteY(note.time, currentTime, scrollSpeed, baseBPM, bpmScaleMode, timingPoints, ignoreSV);

        // Only clamp head to judge line if actively holding (not missed or released)
        if ((note.state == NoteState::Holding || note.state == NoteState::Released) && !note.hadComboBreak) {
            y = judgeLineY - NOTE_HEIGHT;
        } else if (y > windowHeight) {
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

        // Texture color mod: white normally, gray when missed
        Uint8 texMod = 255;
        if (note.state == NoteState::Missed || note.hadComboBreak) {
            texMod = 128;  // Gray for missed notes
        }

        float laneW = getLaneWidth(note.lane);
        float x = (float)getLaneX(note.lane) + 2;
        float w = laneW - 4;

        if (note.isHold && note.endTime > note.time) {
            int endY = getNoteY(note.endTime, currentTime, scrollSpeed, baseBPM, bpmScaleMode, timingPoints, ignoreSV);

            if (endY < -NOTE_HEIGHT && y < -NOTE_HEIGHT) continue;

            // Calculate tail alpha based on its position
            Uint8 tailAlpha = calcHiddenAlpha(endY);

            // Skip entire hold note if both head and tail are hidden
            if (alpha == 0 && tailAlpha == 0) continue;

            // Get head texture height to calculate body bottom position
            float headH = (float)NOTE_HEIGHT;
            SDL_Texture* headTex = skinManager ? skinManager->getNoteHeadTexture(note.lane, keyCount) : nullptr;
            if (headTex) {
                float texW, texH;
                SDL_GetTextureSize(headTex, &texW, &texH);
                headH = w * (texH / texW);
            }
            float headTop = y + NOTE_HEIGHT - headH;
            float headCenter = headTop + headH / 2;  // Head center position

            // Get tail texture height
            float tailH = (float)NOTE_HEIGHT;
            SDL_Texture* tailTex = skinManager ? skinManager->getNoteTailTexture(note.lane, keyCount) : nullptr;
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

                // Render body if any part is visible
                if (renderBottom > renderTop) {
                    // Calculate texture offset for off-screen portion
                    float srcOffsetY = (float)(renderTop - fullBodyTop);
                    float renderHeight = (float)(renderBottom - renderTop);

                    SDL_Texture* bodyTex = skinManager ? skinManager->getNoteBodyTexture(note.lane, keyCount) : nullptr;
                    if (bodyTex) {
                        SDL_SetTextureAlphaMod(bodyTex, 255);
                        SDL_SetTextureColorMod(bodyTex, texMod, texMod, texMod);  // Gray when missed
                        float texW, texH;
                        SDL_GetTextureSize(bodyTex, &texW, &texH);

                        // Calculate source rect with proper clamping
                        float srcY = std::max(0.0f, srcOffsetY);
                        float srcH = std::min(texH, renderHeight);

                        // If srcOffsetY exceeds texture height, tile from beginning
                        if (srcOffsetY >= texH) {
                            srcY = fmod(srcOffsetY, texH);
                            srcH = std::min(texH - srcY, renderHeight);
                        }

                        SDL_FRect srcRect = { 0, srcY, texW, srcH };
                        SDL_FRect dstRect = { (float)x, (float)renderTop, (float)w, renderHeight };
                        SDL_RenderTexture(renderer, bodyTex, &srcRect, &dstRect);
                    } else {
                        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 64);
                        SDL_FRect body = { (float)x, (float)renderTop, (float)w, renderHeight };
                        SDL_RenderFillRect(renderer, &body);
                    }
                }
            }

            // Render tail with its own alpha (bottom aligned to body top)
            if (endY >= -NOTE_HEIGHT * 3 && endY <= windowHeight && tailAlpha > 0) {
                if (tailTex) {
                    float tailY = (float)(endY + NOTE_HEIGHT) - tailH;  // Tail bottom aligns to body top
                    SDL_SetTextureAlphaMod(tailTex, tailAlpha);
                    SDL_SetTextureColorMod(tailTex, texMod, texMod, texMod);  // Gray when missed
                    SDL_FRect tail = { (float)x, tailY, (float)w, tailH };
                    // Flip tail texture vertically (osu! default behavior)
                    SDL_RenderTextureRotated(renderer, tailTex, nullptr, &tail, 0, nullptr, SDL_FLIP_VERTICAL);
                } else {
                    Uint8 tailBodyAlpha = (Uint8)(64 * tailAlpha / 255);
                    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, tailBodyAlpha);
                    SDL_FRect tail = { (float)x, (float)endY, (float)w, (float)NOTE_HEIGHT };
                    SDL_RenderFillRect(renderer, &tail);
                }
            }
        }

        // Skip head rendering for fake notes (only tail is rendered)
        if (isFakeNote) continue;

        if (y >= -NOTE_HEIGHT * 3 && y <= windowHeight) {
            // Try to use skin texture, fallback to solid color
            // For hold notes, use getNoteHeadTexture; for regular notes, use getNoteTexture
            bool isHoldNote = (note.endTime > note.time);
            SDL_Texture* noteTex = nullptr;
            if (skinManager) {
                noteTex = isHoldNote ? skinManager->getNoteHeadTexture(note.lane, keyCount)
                                     : skinManager->getNoteTexture(note.lane, keyCount);
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

    float elementScale = getElementScale();
    float finalScale = elementScale * animScale;
    float w = texW * finalScale;
    float h = texH * finalScale;
    float centerX = stageX + totalWidth / 2;

    // Use skin config scorePosition or default
    const ManiaConfig* cfg = skinManager ? skinManager->getManiaConfig(keyCount) : nullptr;
    float centerY = cfg ? cfg->scorePosition * baseScale : (float)judgeLineY - 120;

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

void Renderer::renderSpeedInfo(int scrollSpeed, bool bpmScaleMode, bool autoPlay) {
    if (!font) return;
    char buf[64];
    const char* modeStr = bpmScaleMode ? "BPM" : "Fixed";
    if (autoPlay) {
        snprintf(buf, sizeof(buf), "Speed: %d (%s) [AUTO]", scrollSpeed, modeStr);
    } else {
        snprintf(buf, sizeof(buf), "Speed: %d (%s) [Tab:Auto]", scrollSpeed, modeStr);
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

void Renderer::renderCombo(int combo, int64_t comboAnimTime, bool comboBreak, int64_t breakAnimTime) {
    if (combo < 2 && !comboBreak) return;

    const ManiaConfig* cfg = skinManager ? skinManager->getManiaConfig(keyCount) : nullptr;
    float baseScale = (float)windowHeight / 480.0f;
    float elementScale = getElementScale();
    float comboY = cfg ? cfg->comboPosition * baseScale : (judgeLineY - 100);

    // Calculate stage center
    float stageX = (float)getLaneX(0);
    float totalWidth = 0;
    for (int i = 0; i < keyCount; i++) {
        totalWidth += getLaneWidth(i);
        if (i < keyCount - 1) totalWidth += getColumnSpacing(i);
    }
    float centerX = stageX + totalWidth / 2;

    // Handle combo break animation (fade out + scale up)
    if (comboBreak && breakAnimTime < 200) {
        float t = (float)breakAnimTime / 200.0f;
        float alpha = 0.8f * (1.0f - t);
        float breakScale = 1.0f + 3.0f * t;  // 1->4
        // Render the old combo with break animation using font
        // (skin digits would need the old combo value stored)
        return;  // Skip for now, just hide during break
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

        for (int i = 0; i < len; i++) {
            int digit = buf[i] - '0';
            SDL_Texture* tex = skinManager->getComboDigitTexture(digit);
            if (!tex) {
                useSkinDigits = false;
                break;
            }
            float texW, texH;
            SDL_GetTextureSize(tex, &texW, &texH);
            digitTextures.push_back(tex);
            digitWidths.push_back(texW * elementScale);
            digitTotalW += texW * elementScale;
        }

        if (useSkinDigits && !digitTextures.empty()) {
            float startX = centerX - digitTotalW / 2;
            float curX = startX;

            for (size_t i = 0; i < digitTextures.size(); i++) {
                SDL_Texture* tex = digitTextures[i];
                float texW, texH;
                SDL_GetTextureSize(tex, &texW, &texH);

                float w = texW * elementScale;
                float h = texH * elementScale * scaleY;
                float y = comboY - (h - texH * elementScale) / 2;  // Adjust for Y scale

                SDL_FRect dst = { curX, y, w, h };
                SDL_RenderTexture(renderer, tex, nullptr, &dst);
                curX += w;
            }
            return;
        }
    }

    // Fallback to font rendering
    if (!font) return;

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", combo);
    SDL_Color yellow = {255, 255, 0, 255};
    SDL_Surface* surface = TTF_RenderText_Blended(font, buf, strlen(buf), yellow);
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
        float bgW, bgH, fillW, fillH;
        SDL_GetTextureSize(bgTex, &bgW, &bgH);
        SDL_GetTextureSize(fillTex, &fillW, &fillH);

        // osu! mania: scale 0.7, extra Y scale = windowHeight/480
        const float MANIA_SCALE = 0.7f;
        float extraScale = (float)windowHeight / 480.0f;

        // Scaled size (before rotation)
        float scaledBgW = bgW * MANIA_SCALE;
        float scaledBgH = bgH * MANIA_SCALE * extraScale;

        // Position: right of stage, bottom at hit position
        float targetX = stageStartX + stageWidth + 1.0f * extraScale;
        float targetY = skinHitPosition - scaledBgW;  // Top after rotation

        // Calculate dstrect position for -90 degree rotation around center
        // After rotation: visual width = scaledBgH, visual height = scaledBgW
        float dstX = targetX - scaledBgW / 2 + scaledBgH / 2;
        float dstY = targetY - scaledBgH / 2 + scaledBgW / 2;
        SDL_FRect bgDst = { dstX, dstY, scaledBgW, scaledBgH };
        SDL_RenderTextureRotated(renderer, bgTex, nullptr, &bgDst, -90.0, nullptr, SDL_FLIP_NONE);

        // Fill: clip width based on HP, same rotation
        // After rotation, fill should be BOTTOM aligned with background
        if (hpPercent > 0) {
            float clipW = fillW * (float)hpPercent;
            SDL_FRect fillSrc = { 0, 0, clipW, fillH };

            float scaledFillW = clipW * MANIA_SCALE;
            float scaledFillH = fillH * MANIA_SCALE * extraScale;

            // X position: use scaledFillH (not scaledBgH) for correct rotation alignment
            // Offset: +2 right, -4 up
            float fillDstX = targetX - scaledFillW / 2 + scaledFillH / 2 + 17.0f;
            float fillDstY = skinHitPosition - scaledFillH / 2 - scaledFillW / 2 - 3.0f;
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

    float kiW, kiH;
    SDL_GetTextureSize(kiTex, &kiW, &kiH);

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
                                 int64_t window100, int64_t window50, int64_t windowMiss, float scale) {
    // osu!mania style hit error bar

    // Bar dimensions per osu! spec
    float barWidth = (float)windowMiss * scale;  // 最大判定窗口 × 缩放
    float barHeight = 3.0f * scale;              // 基础高度
    float bgHeight = barHeight * 4.0f;           // 背景高度 = 基础高度 × 4
    float barX = (float)(windowWidth / 2) - barWidth / 2;
    float barY = (float)(windowHeight) - bgHeight;  // 屏幕底部

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

    // Draw window zones from outside to inside
    // Miss zone (red)
    SDL_SetRenderDrawColor(renderer, colorMiss.r, colorMiss.g, colorMiss.b, 128);
    SDL_FRect missZone = { barX, zoneY, barWidth, barHeight };
    SDL_RenderFillRect(renderer, &missZone);

    // 100/50 zone (yellow)
    float zone100Width = (float)window100 / (float)windowMiss * halfWidth;
    SDL_SetRenderDrawColor(renderer, color100.r, color100.g, color100.b, 128);
    SDL_FRect zone100 = { centerX - zone100Width, zoneY, zone100Width * 2.0f, barHeight };
    SDL_RenderFillRect(renderer, &zone100);

    // 200 zone (green)
    float zone200Width = (float)window200 / (float)windowMiss * halfWidth;
    SDL_SetRenderDrawColor(renderer, color200.r, color200.g, color200.b, 128);
    SDL_FRect zone200 = { centerX - zone200Width, zoneY, zone200Width * 2.0f, barHeight };
    SDL_RenderFillRect(renderer, &zone200);

    // 300 zone (blue)
    float zone300Width = (float)window300 / (float)windowMiss * halfWidth;
    SDL_SetRenderDrawColor(renderer, color300.r, color300.g, color300.b, 128);
    SDL_FRect zone300 = { centerX - zone300Width, zoneY, zone300Width * 2.0f, barHeight };
    SDL_RenderFillRect(renderer, &zone300);

    // 300g zone (rose)
    float zone300gWidth = (float)window300g / (float)windowMiss * halfWidth;
    SDL_SetRenderDrawColor(renderer, color300g.r, color300g.g, color300g.b, 128);
    SDL_FRect zone300g = { centerX - zone300gWidth, zoneY, zone300gWidth * 2.0f, barHeight };
    SDL_RenderFillRect(renderer, &zone300g);

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

        // Select color based on offset
        int64_t absOff = std::abs(err.offset);
        Uint8 r, g, b;
        if (absOff <= window300g) {
            r = color300g.r; g = color300g.g; b = color300g.b;
        } else if (absOff <= window300) {
            r = color300.r; g = color300.g; b = color300.b;
        } else if (absOff <= window200) {
            r = color200.r; g = color200.g; b = color200.b;
        } else if (absOff <= window100) {
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
        // Check if there's a new hit
        if (errors.size() != lastHitErrorCount) {
            lastHitErrorCount = errors.size();

            // Find the most recent error
            int64_t mostRecentTime = 0;
            float mostRecentOffset = 0;
            for (const auto& err : errors) {
                if (err.time > mostRecentTime) {
                    mostRecentTime = err.time;
                    mostRecentOffset = (float)err.offset;
                }
            }

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

    const char* version = "Version 0.0.1b";
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
                              float x, float y, float w, int mouseX, int mouseY, bool clicked, bool& expanded) {
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
    }

    if (expanded) {
        for (int i = 0; i < optionCount; i++) {
            float optY = y + h + i * h;
            bool hoverOpt = mouseX >= x && mouseX <= x + w && mouseY >= optY && mouseY <= optY + h;
            SDL_SetRenderDrawColor(renderer, hoverOpt ? 70 : 50, hoverOpt ? 70 : 50, hoverOpt ? 90 : 70, 255);
            SDL_FRect optRect = {x, optY, w, h};
            SDL_RenderFillRect(renderer, &optRect);

            if (font) {
                SDL_Color white = {255, 255, 255, 255};
                SDL_Surface* s = TTF_RenderText_Blended(font, options[i], strlen(options[i]), white);
                if (s) {
                    SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
                    SDL_FRect dst = {x + 8, optY + (h - s->h) / 2, (float)s->w, (float)s->h};
                    SDL_RenderTexture(renderer, t, nullptr, &dst);
                    SDL_DestroyTexture(t);
                    SDL_DestroySurface(s);
                }
            }

            if (hoverOpt && clicked) {
                newSelected = i;
                expanded = false;
            }
        }
    }

    return newSelected;
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

void Renderer::renderPauseMenu(int selection) {
    // Dark overlay
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_FRect overlay = {0, 0, (float)windowWidth, (float)windowHeight};
    SDL_RenderFillRect(renderer, &overlay);

    if (!font) return;

    const char* options[] = {"Resume", "Retry", "Exit"};
    float menuY = 280;
    float lineHeight = 60;

    // Title
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* titleSurf = TTF_RenderText_Blended(font, "PAUSED", 6, white);
    if (titleSurf) {
        SDL_Texture* titleTex = SDL_CreateTextureFromSurface(renderer, titleSurf);
        float tx = (float)(windowWidth / 2 - titleSurf->w / 2);
        SDL_FRect dst = {tx, 200, (float)titleSurf->w, (float)titleSurf->h};
        SDL_RenderTexture(renderer, titleTex, nullptr, &dst);
        SDL_DestroyTexture(titleTex);
        SDL_DestroySurface(titleSurf);
    }

    // Menu options
    for (int i = 0; i < 3; i++) {
        SDL_Color color = (i == selection) ? SDL_Color{255, 255, 0, 255} : white;
        const char* prefix = (i == selection) ? "> " : "  ";
        char buf[32];
        snprintf(buf, sizeof(buf), "%s%s", prefix, options[i]);

        SDL_Surface* s = TTF_RenderText_Blended(font, buf, strlen(buf), color);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
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

void Renderer::renderDeathMenu(int selection, float slowdown) {
    // Dark red overlay with slowdown-based alpha
    Uint8 alpha = (Uint8)(180 * (1.0f - slowdown * 0.5f));
    SDL_SetRenderDrawColor(renderer, 40, 0, 0, alpha);
    SDL_FRect overlay = {0, 0, (float)windowWidth, (float)windowHeight};
    SDL_RenderFillRect(renderer, &overlay);

    if (!font) return;

    // Title "You Died!"
    SDL_Color red = {255, 60, 60, 255};
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* titleSurf = TTF_RenderText_Blended(font, "You Died!", 9, red);
    if (titleSurf) {
        SDL_Texture* titleTex = SDL_CreateTextureFromSurface(renderer, titleSurf);
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
        SDL_Color color = (i == selection) ? SDL_Color{255, 255, 0, 255} : white;
        const char* prefix = (i == selection) ? "> " : "  ";
        char buf[32];
        snprintf(buf, sizeof(buf), "%s%s", prefix, options[i]);

        SDL_Surface* s = TTF_RenderText_Blended(font, buf, strlen(buf), color);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
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
                                int mouseX, int mouseY, bool clicked, bool& editing) {
    float h = 30;
    float labelOffset = 0;

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

    // Text content
    if (font) {
        std::string displayText = text.empty() ? "..." : text;
        if (editing) displayText += "_";  // Cursor
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface* s = TTF_RenderText_Blended(font, displayText.c_str(), displayText.length(), white);
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
            SDL_FRect dst = {x + 8, inputY + (h - s->h) / 2, (float)s->w, (float)s->h};
            SDL_RenderTexture(renderer, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            SDL_DestroySurface(s);
        }
    }

    // Toggle editing on click
    if (clicked) {
        if (hover) {
            editing = true;
        } else {
            editing = false;
        }
    }

    return editing;
}
