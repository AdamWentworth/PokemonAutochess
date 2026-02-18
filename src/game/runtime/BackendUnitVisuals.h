#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/PokemonInstance.h"
#include "game/runtime/BackendImagePath.h"

#include <algorithm>
#include <string>

namespace game::runtime::backend_units {

inline std::string resolveWorldUnitImagePath(const std::string& unitName) {
    return runtime::backend_images::resolvePokemonPortraitPath(
        "",
        unitName,
        "");
}

inline void applyWorldUnitTint(IRenderBackend::DebugQuad& quad, const PokemonInstance& unit) {
    if (unit.side == PokemonSide::Player) {
        quad.r = 0.16f;
        quad.g = 0.84f;
        quad.b = 0.40f;
    } else {
        quad.r = 0.90f;
        quad.g = 0.28f;
        quad.b = 0.22f;
    }

    if (!unit.alive && unit.captureInProgress) {
        quad.r = 0.98f;
        quad.g = 0.82f;
        quad.b = 0.30f;
    } else if (!unit.alive) {
        quad.r *= 0.45f;
        quad.g *= 0.45f;
        quad.b *= 0.45f;
    }
    quad.a = 0.26f;
}

inline IRenderBackend::DebugSprite makeWorldUnitSprite(float centerX,
                                                       float centerY,
                                                       float cellW,
                                                       float cellH,
                                                       const std::string& imagePath,
                                                       float alpha = 1.0f) {
    IRenderBackend::DebugSprite sprite;
    if (imagePath.empty()) return sprite;

    const float spriteW = std::max(6.0f, cellW * 0.50f);
    const float spriteH = std::max(6.0f, cellH * 0.50f);
    sprite.x = centerX - spriteW * 0.5f;
    sprite.y = centerY - spriteH * 0.5f;
    sprite.w = spriteW;
    sprite.h = spriteH;
    sprite.u0 = 0.0f;
    sprite.v0 = 0.0f;
    sprite.u1 = 1.0f;
    sprite.v1 = 1.0f;
    sprite.r = 1.0f;
    sprite.g = 1.0f;
    sprite.b = 1.0f;
    sprite.a = std::clamp(alpha, 0.0f, 1.0f);
    sprite.texturePath = imagePath;
    return sprite;
}

inline IRenderBackend::DebugSprite makeBenchUnitSprite(float x,
                                                       float y,
                                                       float w,
                                                       float h,
                                                       const std::string& imagePath,
                                                       float alpha = 1.0f) {
    IRenderBackend::DebugSprite sprite;
    if (imagePath.empty()) return sprite;

    const float inset = std::max(1.0f, std::min(w, h) * 0.15f);
    sprite.x = x + inset;
    sprite.y = y + inset;
    sprite.w = std::max(4.0f, w - inset * 2.0f);
    sprite.h = std::max(4.0f, h - inset * 2.0f);
    sprite.u0 = 0.0f;
    sprite.v0 = 0.0f;
    sprite.u1 = 1.0f;
    sprite.v1 = 1.0f;
    sprite.r = 1.0f;
    sprite.g = 1.0f;
    sprite.b = 1.0f;
    sprite.a = std::clamp(alpha, 0.0f, 1.0f);
    sprite.texturePath = imagePath;
    return sprite;
}

} // namespace game::runtime::backend_units
