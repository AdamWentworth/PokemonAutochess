#include "game/runtime/BackendTopBanner.h"

#include <string>
#include <vector>

bool test_backend_top_banner_contract(std::string& outFail) {
    using game::runtime::top_banner::Style;
    using game::runtime::top_banner::appendBackendBanner;
    using game::runtime::top_banner::computeLayout;
    using game::runtime::top_banner::topTextY;

    if (topTextY(720) != 58.0f) {
        outFail = "top banner Y expected 58 at 720p";
        return false;
    }

    const auto layout = computeLayout(1280, 720, 320.0f, 40.0f);
    if (layout.textX < 0.0f || layout.textY < 0.0f) {
        outFail = "layout text anchor must stay on-screen";
        return false;
    }
    if (layout.panel.w <= 0.0f || layout.panel.h <= 0.0f) {
        outFail = "layout panel dimensions must be positive";
        return false;
    }
    if (layout.panel.x > layout.textX || layout.panel.y > layout.textY) {
        outFail = "layout panel must include text origin";
        return false;
    }

    std::vector<IRenderBackend::DebugQuad> quads;
    std::vector<IRenderBackend::DebugLine> lines;
    Style style;
    style.textScale = 1.95f;
    appendBackendBanner(quads, lines, 1280, 720, "Banner Contract", 1.0f, 0.9f, 0.7f, style);
    if (quads.empty()) {
        outFail = "appendBackendBanner should emit panel quad";
        return false;
    }
    if (lines.empty()) {
        outFail = "appendBackendBanner should emit text line geometry";
        return false;
    }

    return true;
}

