#include "game/runtime/session/SessionRenderLayout.h"

#include "game/runtime/ui/UiScale.h"

#include <algorithm>

namespace game::runtime::session_render_layout {

Layout build(const GameConfigData& config, int drawableW, int drawableH) {
    Layout out;
    out.rows = std::max(1, config.rows);
    out.cols = std::max(1, config.cols);
    out.minDim = static_cast<float>(std::min(drawableW, drawableH));
    out.uiScale = runtime::ui_scale::viewportScale(drawableW, drawableH);
    out.edgePad = runtime::ui_scale::edgePad(drawableW, drawableH);
    out.lineStep = runtime::ui_scale::lineStep(drawableW, drawableH);
    out.boardW = std::max(240.0f, out.minDim * 0.78f);
    out.boardH = std::max(180.0f, out.minDim * 0.58f);
    out.boardX = (static_cast<float>(drawableW) - out.boardW) * 0.5f;
    out.boardY = (static_cast<float>(drawableH) - out.boardH) * 0.5f;
    out.cellW = out.boardW / static_cast<float>(out.cols);
    out.cellH = out.boardH / static_cast<float>(out.rows);
    out.sharedUnitHudCfg = {config.xpLevelBase, config.xpLevelGrowth};
    return out;
}

} // namespace game::runtime::session_render_layout
