#include "game/runtime/session/SessionRenderLayout.h"

#include <string>

bool test_session_render_layout_contract(std::string& outFail) {
    GameConfigData cfg;
    cfg.rows = 0;
    cfg.cols = -5;
    cfg.xpLevelBase = 12;
    cfg.xpLevelGrowth = 1.6f;

    const auto layout = game::runtime::session_render_layout::build(cfg, 1280, 720);

    if (layout.rows != 1 ||
        layout.cols != 1 ||
        layout.minDim != 720.0f ||
        layout.boardW <= 0.0f ||
        layout.boardH <= 0.0f ||
        layout.cellW != layout.boardW ||
        layout.cellH != layout.boardH) {
        outFail =
            "SessionRenderLayout should clamp row and column counts and derive board dimensions from the viewport.";
        return false;
    }

    if (layout.sharedUnitHudCfg.xpLevelBase != 12 ||
        layout.sharedUnitHudCfg.xpLevelGrowth != 1.6f) {
        outFail =
            "SessionRenderLayout should preserve shared HUD progression settings from GameConfigData.";
        return false;
    }

    return true;
}
