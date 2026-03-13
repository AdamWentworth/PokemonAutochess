#pragma once

#include <algorithm>
#include <cmath>

namespace game::runtime::ui_scale {

inline float viewportScale(int uiW, int uiH) {
    if (uiW <= 0 || uiH <= 0) return 1.0f;
    const float sx = static_cast<float>(uiW) / 1280.0f;
    const float sy = static_cast<float>(uiH) / 720.0f;
    const float scale = std::min(sx, sy);
    return std::clamp(scale, 0.72f, 1.45f);
}

inline float scaled(float value, float scale, float minValue, float maxValue) {
    return std::clamp(std::round(value * scale), minValue, maxValue);
}

inline float edgePad(int uiW, int uiH, float base = 24.0f) {
    const float scale = viewportScale(uiW, uiH);
    return scaled(base, scale, 10.0f, 48.0f);
}

inline float lineStep(int uiW, int uiH, float base = 16.0f) {
    const float scale = viewportScale(uiW, uiH);
    return scaled(base, scale, 12.0f, 28.0f);
}

} // namespace game::runtime::ui_scale


