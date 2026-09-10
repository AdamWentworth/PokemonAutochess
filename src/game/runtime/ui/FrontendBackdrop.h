#pragma once

#include "engine/render/IRenderBackend.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace game::runtime::ui_frontend {

// Fill without stretching. An optional focus and zoom animate the framing of
// the authored image; clamp the crop so no viewport aspect exposes empty edges.
inline IRenderBackend::DebugSprite backdropSprite(const std::string &path,
                                                  float imageAspect,
                                                  int width,
                                                  int height,
                                                  float centerU = 0.5f,
                                                  float centerV = 0.5f,
                                                  float zoom = 1.0f) {
    IRenderBackend::DebugSprite sprite;
    sprite.texturePath = path;
    sprite.w = static_cast<float>(std::max(1, width));
    sprite.h = static_cast<float>(std::max(1, height));
    if (!std::isfinite(imageAspect) || imageAspect <= 0.0f) imageAspect = 1.6f;
    const float viewportAspect = sprite.w / sprite.h;
    if (viewportAspect < imageAspect) {
        const float visible = viewportAspect / imageAspect;
        sprite.u0 = (1.0f - visible) * 0.5f;
        sprite.u1 = 1.0f - sprite.u0;
    } else {
        const float visible = imageAspect / viewportAspect;
        sprite.v0 = (1.0f - visible) * 0.5f;
        sprite.v1 = 1.0f - sprite.v0;
    }
    if (!std::isfinite(zoom)) zoom = 1.0f;
    zoom = std::clamp(zoom, 1.0f, 4.0f);
    if (!std::isfinite(centerU)) centerU = .5f;
    if (!std::isfinite(centerV)) centerV = .5f;
    const float halfU = (sprite.u1 - sprite.u0) / (2.0f * zoom);
    const float halfV = (sprite.v1 - sprite.v0) / (2.0f * zoom);
    centerU = std::clamp(centerU, halfU, 1.0f - halfU);
    centerV = std::clamp(centerV, halfV, 1.0f - halfV);
    sprite.u0 = centerU - halfU;
    sprite.u1 = centerU + halfU;
    sprite.v0 = centerV - halfV;
    sprite.v1 = centerV + halfV;
    return sprite;
}

} // namespace game::runtime::ui_frontend
