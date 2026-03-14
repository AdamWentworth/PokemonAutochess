#include "game/runtime/shared/ui/SharedUnitHudBatches.h"

#include "game/runtime/ui/DebugText.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

namespace game::runtime::shared_unit_hud {
namespace {

int xpToNextLevel(const Config& config, int level) {
    if (config.xpLevelBase <= 0) return 0;
    const int useLevel = std::max(1, level);
    const float growth = (config.xpLevelGrowth > 0.0f) ? config.xpLevelGrowth : 1.0f;
    const float raw =
        static_cast<float>(config.xpLevelBase) * std::pow(growth, static_cast<float>(useLevel - 1));
    return std::max(1, static_cast<int>(std::round(raw)));
}

struct CachedTextMetrics {
    bool boundsValid = false;
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

const CachedTextMetrics& cachedTextMetrics(const std::string& text) {
    static thread_local std::unordered_map<std::string, CachedTextMetrics> cache;

    auto it = cache.find(text);
    if (it != cache.end()) {
        return it->second;
    }

    CachedTextMetrics metrics{};
    const runtime::ui_text::TextBounds bounds =
        runtime::ui_text::measureTextBounds(text, 1.0f);
    metrics.boundsValid = bounds.valid;
    metrics.minX = bounds.minX;
    metrics.minY = bounds.minY;
    metrics.maxX = bounds.maxX;
    metrics.maxY = bounds.maxY;
    if (bounds.valid) {
        metrics.width = std::max(0.0f, bounds.maxX - bounds.minX);
        metrics.height = std::max(0.0f, bounds.maxY - bounds.minY);
    } else {
        metrics.width = std::max(0.0f, runtime::ui_text::measureTextWidth(text, 1.0f));
        metrics.height = std::max(0.0f, runtime::ui_text::measureTextHeight(text, 1.0f));
    }
    return cache.emplace(text, metrics).first->second;
}

void appendRingArc(std::vector<IRenderBackend::DebugLine>& lines,
                   const glm::vec2& center,
                   float innerR,
                   float outerR,
                   float startRad,
                   float endRad,
                   float r,
                   float g,
                   float b,
                   float a) {
    const float arc = endRad - startRad;
    if (std::abs(arc) < 0.0001f) return;
    const float full = 6.2831853f;
    const int segments = std::max(6, static_cast<int>(std::ceil(std::abs(arc) / full * 32.0f)));
    const float thickness = std::max(1.0f, outerR - innerR);
    for (int i = 0; i < segments; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(segments);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments);
        const float a0 = startRad + arc * t0;
        const float a1 = startRad + arc * t1;
        IRenderBackend::DebugLine lineSeg;
        lineSeg.x1 = center.x + std::cos(a0) * (innerR + outerR) * 0.5f;
        lineSeg.y1 = center.y + std::sin(a0) * (innerR + outerR) * 0.5f;
        lineSeg.x2 = center.x + std::cos(a1) * (innerR + outerR) * 0.5f;
        lineSeg.y2 = center.y + std::sin(a1) * (innerR + outerR) * 0.5f;
        lineSeg.thickness = thickness;
        lineSeg.r = r;
        lineSeg.g = g;
        lineSeg.b = b;
        lineSeg.a = a;
        lines.push_back(lineSeg);
    }
}

} // namespace

void appendLegacyUnitHud(std::vector<IRenderBackend::DebugQuad>& worldQuads,
                         std::vector<IRenderBackend::DebugLine>& lines,
                         std::vector<IRenderBackend::DebugLine>& textLines,
                         const Config& config,
                         const PokemonInstance& unit,
                         float screenX,
                         float screenY,
                         float cellPx) {
    const float safeCellPx = std::max(8.0f, cellPx);
    const float hudScale = 1.20f; // Keep shared HUD ~20% larger to match legacy readability.
    const float hudPx = safeCellPx * hudScale;
    const float width = hudPx * 0.45f;
    const float hpH = hudPx * 0.07f;
    const float enH = hudPx * 0.06f;
    const float yOffset = hudPx * 0.82f;
    const float gap = hudPx * 0.03f;

    const float ringOuter = hudPx * 0.155f;
    const float ringInner = hudPx * 0.135f;
    const float ringGap = hudPx * 0.035f;
    const float ringPad = ringOuter + ringGap;
    const float leftExtent = ringOuter + ringPad;

    glm::vec2 pos(screenX, screenY);
    pos.x -= (width - leftExtent) * 0.5f;
    pos.y -= yOffset;

    const float hpRatio = std::clamp(
        static_cast<float>(std::max(0, unit.hp)) / static_cast<float>(std::max(1, unit.maxHP)),
        0.0f,
        1.0f);
    const float energyRatio = std::clamp(
        static_cast<float>(std::max(0, unit.energy)) / static_cast<float>(std::max(1, unit.maxEnergy)),
        0.0f,
        1.0f);

    IRenderBackend::DebugQuad hpBg;
    hpBg.x = pos.x;
    hpBg.y = pos.y;
    hpBg.w = width;
    hpBg.h = hpH;
    hpBg.r = 0.3f;
    hpBg.g = 0.3f;
    hpBg.b = 0.3f;
    hpBg.a = 1.0f;
    worldQuads.push_back(hpBg);

    IRenderBackend::DebugQuad hpFg = hpBg;
    hpFg.w = width * hpRatio;
    if (unit.side == PokemonSide::Enemy) {
        hpFg.r = 1.0f;
        hpFg.g = 0.0f;
        hpFg.b = 0.0f;
    } else {
        hpFg.r = 0.0f;
        hpFg.g = 1.0f;
        hpFg.b = 0.0f;
    }
    worldQuads.push_back(hpFg);

    IRenderBackend::DebugQuad energyBg = hpBg;
    energyBg.y = pos.y + hpH + gap;
    energyBg.h = enH;
    energyBg.r = 0.25f;
    energyBg.g = 0.25f;
    energyBg.b = 0.25f;
    worldQuads.push_back(energyBg);

    IRenderBackend::DebugQuad energyFg = energyBg;
    energyFg.w = width * energyRatio;
    energyFg.r = 0.95f;
    energyFg.g = 0.65f;
    energyFg.b = 0.20f;
    worldQuads.push_back(energyFg);

    const float barH = hpH + gap + enH;
    const glm::vec2 levelCenter(pos.x - ringPad, pos.y + barH * 0.5f - hudPx * 0.02f);
    const bool showXP = (unit.side == PokemonSide::Player);
    const int maxXP = showXP ? xpToNextLevel(config, unit.level) : 0;
    if (showXP && maxXP > 0) {
        const float xFrac = std::clamp(
            static_cast<float>(std::max(0, unit.xp)) / static_cast<float>(std::max(1, maxXP)),
            0.0f,
            1.0f);
        const float start = -1.5707963f;
        appendRingArc(lines, levelCenter, ringInner, ringOuter, start, start + 6.2831853f, 0.20f, 0.20f, 0.20f, 1.0f);
        appendRingArc(
            lines,
            levelCenter,
            ringInner,
            ringOuter,
            start,
            start + xFrac * 6.2831853f,
            0.20f,
            0.55f,
            1.0f,
            1.0f);
    }

    const std::string levelText = std::to_string(std::max(1, unit.level));
    const CachedTextMetrics& levelMetrics = cachedTextMetrics(levelText);
    const float baseLevelH = std::max(1.0f, levelMetrics.height);
    const float levelScale = std::clamp((ringInner * 1.55f) / baseLevelH, 0.72f, 1.05f);
    const float levelCenterOffsetX = levelMetrics.boundsValid
        ? ((levelMetrics.minX + levelMetrics.maxX) * 0.5f * levelScale)
        : (std::max(1.0f, levelMetrics.width * levelScale) * 0.5f);
    const float levelCenterOffsetY = levelMetrics.boundsValid
        ? ((levelMetrics.minY + levelMetrics.maxY) * 0.5f * levelScale)
        : (std::max(1.0f, levelMetrics.height * levelScale) * 0.5f);
    const float textX = levelCenter.x - levelCenterOffsetX;
    const float textY = levelCenter.y - levelCenterOffsetY;
    runtime::ui_text::appendTextLines(
        textLines,
        textX,
        textY,
        levelText,
        levelScale,
        1.0f,
        1.0f,
        1.0f,
        1.0f,
        0.88f);
}

} // namespace game::runtime::shared_unit_hud




