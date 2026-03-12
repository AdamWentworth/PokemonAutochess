#include "game/runtime/RuntimeBootLoading.h"

#include <algorithm>

namespace game::runtime::boot_loading {

bool shouldAbortPreloadEvent(const SDL_Event& event) {
    return event.type == SDL_QUIT ||
           (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE);
}

bool buildFallbackLoadingQuads(
    int drawableW,
    int drawableH,
    float progress01,
    std::array<IRenderBackend::DebugQuad, kFallbackLoadingQuadCount>& out) {
    if (drawableW <= 0 || drawableH <= 0) {
        return false;
    }

    const float progress = std::clamp(progress01, 0.0f, 1.0f);
    const float sw = static_cast<float>(drawableW);
    const float sh = static_cast<float>(drawableH);
    const float panelW = std::max(280.0f, sw * 0.42f);
    const float panelH = std::max(120.0f, sh * 0.20f);
    const float panelX = (sw - panelW) * 0.5f;
    const float panelY = (sh - panelH) * 0.5f;
    const float pad = std::max(10.0f, panelH * 0.16f);
    const float barW = std::max(120.0f, panelW - pad * 2.0f);
    const float barH = std::max(12.0f, panelH * 0.22f);
    const float barX = panelX + pad;
    const float barY = panelY + panelH - pad - barH;

    out[0] = IRenderBackend::DebugQuad{0.0f, 0.0f, sw, sh, 0.03f, 0.03f, 0.04f, 1.0f};
    out[1] = IRenderBackend::DebugQuad{panelX, panelY, panelW, panelH, 0.10f, 0.10f, 0.12f, 0.97f};
    out[2] = IRenderBackend::DebugQuad{
        panelX + 2.0f, panelY + 2.0f, panelW - 4.0f, panelH - 4.0f, 0.14f, 0.14f, 0.17f, 0.98f};
    out[3] = IRenderBackend::DebugQuad{barX, barY, barW, barH, 0.22f, 0.22f, 0.26f, 1.0f};
    out[4] = IRenderBackend::DebugQuad{
        barX + 2.0f,
        barY + 2.0f,
        std::max(0.0f, (barW - 4.0f) * progress),
        std::max(0.0f, barH - 4.0f),
        0.77f,
        0.77f,
        0.81f,
        1.0f};
    return true;
}

} // namespace game::runtime::boot_loading
