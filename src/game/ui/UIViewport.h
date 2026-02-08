// src/game/ui/UIViewport.h
#pragma once

#include <algorithm>
#include <cmath>

namespace game::ui {

struct UIViewport {
    int width = 1280;
    int height = 720;
    float scale = 1.0f;

    void set(int w, int h) {
        if (w > 0) width = w;
        if (h > 0) height = h;
        scale = (height > 0) ? (static_cast<float>(height) / 720.0f) : 1.0f;
        scale = std::max(0.25f, std::min(scale, 4.0f));
    }

    float centerX(float elementWidth) const {
        return std::round((static_cast<float>(width) - elementWidth) * 0.5f);
    }
};

} // namespace game::ui
