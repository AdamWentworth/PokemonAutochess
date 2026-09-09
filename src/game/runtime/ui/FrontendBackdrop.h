#pragma once

#include "engine/render/IRenderBackend.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace game::runtime::ui_frontend {

// Fill the viewport without stretching the authored camera image. Crop equally
// at opposing edges so the central selection table stays centered on resize.
inline IRenderBackend::DebugSprite backdropSprite(const std::string &path,
                                                  float imageAspect,
                                                  int width,
                                                  int height) {
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
    return sprite;
}

} // namespace game::runtime::ui_frontend
