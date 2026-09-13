#pragma once

#include "game/runtime/ui/DebugText.h"
#include <cmath>
#include <glm/glm.hpp>

namespace game::runtime::hud_paint {

inline void quad(std::vector<IRenderBackend::DebugQuad> &out, float x, float y,
                 float w, float h, glm::vec3 color, float alpha = 1.0f) {
    IRenderBackend::DebugQuad q;
    q.x = x;
    q.y = y;
    q.w = w;
    q.h = h;
    q.r = color.r;
    q.g = color.g;
    q.b = color.b;
    q.a = alpha;
    out.push_back(q);
}

inline void panel(std::vector<IRenderBackend::DebugQuad> &out,
                  float x, float y, float w, float h, float radius = 8.0f,
                  glm::vec3 color = {0.055f, 0.10f, 0.095f}, float alpha = 0.94f) {
    const float r = std::min(radius, std::min(w, h) * 0.5f);
    // Non-overlapping strips keep translucent rounded corners consistent on
    // every backend without introducing a texture or an extra render pass.
    const int rows = std::max(1, static_cast<int>(std::ceil(r)));
    const float step = r / rows;
    for (int i = 0; i < rows; ++i) {
        const float dy = r - (i + 0.5f) * step;
        const float inset = r - std::sqrt(std::max(0.0f, r * r - dy * dy));
        quad(out, x + inset, y + i * step, w - 2 * inset, step, color, alpha);
        quad(out, x + inset, y + h - (i + 1) * step, w - 2 * inset, step, color, alpha);
    }
    if (h > 2 * r) quad(out, x, y + r, w, h - 2 * r, color, alpha);
}

inline void text(std::vector<IRenderBackend::DebugLine> &out, float x, float y,
                 const std::string &value, float scale, glm::vec3 color = {0.93f, 0.96f, 0.91f}) {
    ui_text::appendTextLines(out, x, y, value, scale, color.r, color.g, color.b, 1.0f, 0.88f);
}

} // namespace game::runtime::hud_paint
