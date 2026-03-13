#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/runtime/backend_ui/DebugText.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace game::runtime::top_banner {

struct Style {
    float textScale = 2.0f;
    float topYFraction = 0.08f;
    float minTextX = 12.0f;
    float minTextY = 12.0f;
    float panelPadX = 14.0f;
    float panelPadY = 10.0f;
    float panelMinH = 20.0f;
    float panelInset = 8.0f;
    float panelR = 0.08f;
    float panelG = 0.10f;
    float panelB = 0.12f;
    float panelA = 0.82f;
    float textA = 1.0f;
    float textStroke = 0.88f;
};

struct Layout {
    float textX = 0.0f;
    float textY = 0.0f;
    float textW = 0.0f;
    float textH = 0.0f;
    IRenderBackend::DebugQuad panel{};
};

inline float topTextY(int uiH, float topYFraction = 0.08f, float minTextY = 12.0f) {
    const float rawY = std::round(std::max(0.0f, static_cast<float>(uiH)) * topYFraction);
    return std::max(minTextY, rawY);
}

inline float centeredTextX(int uiW, float textW) {
    return std::round((static_cast<float>(uiW) - textW) * 0.5f);
}

inline Layout computeLayout(int uiW,
                            int uiH,
                            float textW,
                            float textH,
                            const Style& style = {}) {
    Layout out;
    out.textW = std::max(1.0f, textW);
    out.textH = std::max(1.0f, textH);
    out.textY = topTextY(uiH, style.topYFraction, style.minTextY);
    out.textX = std::max(style.minTextX, centeredTextX(uiW, out.textW));

    out.panel.x = std::max(style.panelInset, out.textX - style.panelPadX);
    out.panel.y = std::max(style.panelInset, out.textY - style.panelPadY);
    out.panel.w = std::min(
        std::max(1.0f, static_cast<float>(uiW) - out.panel.x - style.panelInset),
        out.textW + style.panelPadX * 2.0f);
    out.panel.h = std::max(style.panelMinH, out.textH + style.panelPadY * 2.0f - 4.0f);
    out.panel.r = style.panelR;
    out.panel.g = style.panelG;
    out.panel.b = style.panelB;
    out.panel.a = style.panelA;
    return out;
}

inline void appendBackendBanner(std::vector<IRenderBackend::DebugQuad>& quads,
                                std::vector<IRenderBackend::DebugLine>& lines,
                                int uiW,
                                int uiH,
                                const std::string& text,
                                float textR,
                                float textG,
                                float textB,
                                const Style& style = {}) {
    const float scale = std::max(0.1f, style.textScale);
    const float textW = ui_text::measureTextWidth(text, scale);
    const float textH = ui_text::measureTextHeight(text, scale);
    const Layout layout = computeLayout(uiW, uiH, textW, textH, style);

    quads.push_back(layout.panel);
    ui_text::appendTextLines(
        lines,
        layout.textX,
        layout.textY,
        text,
        scale,
        textR,
        textG,
        textB,
        style.textA,
        style.textStroke);
}

} // namespace game::runtime::top_banner




