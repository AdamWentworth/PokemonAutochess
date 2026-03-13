#pragma once

#include "game/GameConfig.h"
#include "game/runtime/shared/ui/SharedUnitHudBatches.h"

namespace game::runtime::session_render_layout {

struct Layout {
    int rows = 1;
    int cols = 1;
    float minDim = 0.0f;
    float uiScale = 1.0f;
    float edgePad = 0.0f;
    float lineStep = 0.0f;
    float boardW = 0.0f;
    float boardH = 0.0f;
    float boardX = 0.0f;
    float boardY = 0.0f;
    float cellW = 0.0f;
    float cellH = 0.0f;
    shared_unit_hud::Config sharedUnitHudCfg{};
};

Layout build(const GameConfigData& config, int drawableW, int drawableH);

} // namespace game::runtime::session_render_layout
