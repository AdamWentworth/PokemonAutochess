#pragma once

#include "game/ui/ShopLayout.h"
#include "game/ui/SellOverlayUiPolicy.h"

namespace game::runtime::ui_sell_overlay {

struct Model {
    bool visible = false;
    game::ui::SellDropZoneLayout outer;
    game::ui::SellDropZoneLayout hit;
    game::ui::sell_overlay::Copy copy = game::ui::sell_overlay::makeCopy(true);
    float centerX = 0.0f;
    float titleY = 0.0f;
    float hintY = 0.0f;
};

inline Model buildModel(bool showOverlay,
                        int uiW,
                        int uiH,
                        int dropZoneCardCount,
                        bool useItemLayout,
                        bool sellPaysMoney) {
    Model out;
    if (!showOverlay) return out;

    out.outer = game::ui::computeSellDropZoneLayout(uiW, uiH, dropZoneCardCount, useItemLayout);
    out.hit = game::ui::computeSellDropZoneCenterHitLayout(out.outer);
    if (out.outer.w <= 0 || out.outer.h <= 0) return out;

    out.visible = true;
    out.copy = game::ui::sell_overlay::makeCopy(sellPaysMoney);
    out.centerX = static_cast<float>(out.outer.x) + static_cast<float>(out.outer.w) * 0.5f;
    out.titleY = static_cast<float>(out.outer.y) + 10.0f;
    out.hintY = static_cast<float>(out.outer.y) + static_cast<float>(out.outer.h) * 0.58f;
    return out;
}

} // namespace game::runtime::ui_sell_overlay


