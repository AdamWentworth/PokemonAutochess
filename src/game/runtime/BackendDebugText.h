#pragma once

#include "engine/render/IRenderBackend.h"

#include <algorithm>
#include <string>
#include <vector>

#include <stb_easy_font.h>

namespace game::runtime::backend_text {

struct EasyFontVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    unsigned char color[4] = {255, 255, 255, 255};
};
static_assert(sizeof(EasyFontVertex) == 16, "Unexpected stb_easy_font vertex layout.");

inline float measureTextWidth(const std::string& text, float scale) {
    if (text.empty()) return 0.0f;
    const float safeScale = std::max(0.01f, scale);
    const int baseW = stb_easy_font_width(const_cast<char*>(text.c_str()));
    return std::max(0.0f, static_cast<float>(baseW) * safeScale);
}

inline float measureTextHeight(const std::string& text, float scale) {
    if (text.empty()) return 0.0f;
    const float safeScale = std::max(0.01f, scale);
    const int baseH = stb_easy_font_height(const_cast<char*>(text.c_str()));
    return std::max(0.0f, static_cast<float>(baseH) * safeScale);
}

inline void appendTextQuads(std::vector<IRenderBackend::DebugQuad>& out,
                            float originX,
                            float originY,
                            const std::string& text,
                            float scale,
                            float r,
                            float g,
                            float b,
                            float a) {
    if (text.empty()) return;

    const std::size_t approxBytes = text.size() * 320u + 4096u;
    const std::size_t vertexCount = std::max<std::size_t>(256u, approxBytes / sizeof(EasyFontVertex));
    std::vector<EasyFontVertex> verts(vertexCount);

    const int quadCount = stb_easy_font_print(
        originX,
        originY,
        const_cast<char*>(text.c_str()),
        nullptr,
        verts.data(),
        static_cast<int>(verts.size() * sizeof(EasyFontVertex)));
    if (quadCount <= 0) return;

    out.reserve(out.size() + static_cast<std::size_t>(quadCount));
    const std::size_t maxQuads = std::min<std::size_t>(
        static_cast<std::size_t>(quadCount),
        verts.size() / 4u);

    for (std::size_t i = 0; i < maxQuads; ++i) {
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        for (int v = 0; v < 4; ++v) {
            float x = verts[i * 4u + static_cast<std::size_t>(v)].x;
            float y = verts[i * 4u + static_cast<std::size_t>(v)].y;
            if (scale != 1.0f) {
                x = originX + (x - originX) * scale;
                y = originY + (y - originY) * scale;
            }
            if (v == 0) {
                minX = maxX = x;
                minY = maxY = y;
            } else {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }

        IRenderBackend::DebugQuad q;
        q.x = minX;
        q.y = minY;
        q.w = std::max(0.0f, maxX - minX);
        q.h = std::max(0.0f, maxY - minY);
        if (q.w <= 0.0f || q.h <= 0.0f) continue;
        q.r = r;
        q.g = g;
        q.b = b;
        q.a = a;
        out.push_back(q);
    }
}

} // namespace game::runtime::backend_text

