#pragma once

#include "game/ui/ShopLayout.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace game::runtime::ui_shop_hud {

inline std::string keyboardPrefixedLabel(int slot, const std::string& label) {
    if (slot > 0 && slot <= 9) {
        return "[" + std::to_string(slot) + "] " + label;
    }
    return label;
}

inline std::string moneyLabel(int money) {
    return "Gold: " + std::to_string(std::max(0, money));
}

inline std::string rerollLabel(int slot) {
    return keyboardPrefixedLabel(slot, "Reroll 2g");
}

inline std::string interactionHint() {
    return "Use mouse or keys 1-9";
}

inline int cardsAnchorX(float firstButtonX) {
    return std::max(0, static_cast<int>(std::round(firstButtonX)));
}

inline int cardsAnchorY(float firstButtonY, int uiH) {
    return std::max(0, std::max(static_cast<int>(std::round(firstButtonY)), uiH - 120));
}

inline int cardsAnchorH(float firstButtonH) {
    return std::max(1, static_cast<int>(std::round(firstButtonH)));
}

struct LayoutInput {
    int uiW = 1280;
    int uiH = 720;
    int cardsX = 18;
    int cardsY = 600;
    int cardsH = 96;
    float moneyTextW = 0.0f;
    float moneyTextH = 0.0f;
    float rerollTextW = 0.0f;
    float rerollTextH = 0.0f;
    bool showReroll = false;
};

struct DockRect {
    float x = 0, y = 0, w = 0, h = 0;
};
struct DockLayout {
    DockRect panel, reroll, ready;
    float moneyX = 0, moneyY = 0, scale = 1;
};

inline DockLayout computeDock(int width, int height, float cardsX, float cardsY, float cardsRight) {
    DockLayout out;
    out.scale = std::clamp(std::min(width / 1280.0f, height / 720.0f), .6f, 1.4f);
    const float s = out.scale, margin = 12 * s;
    const float contentW = std::max(cardsRight - cardsX, 440 * s);
    out.panel.w = std::min(width - 2 * margin, contentW + 28 * s);
    out.panel.x = (width - out.panel.w) * .5f;
    out.panel.y = std::max(0.0f, cardsY - 49 * s);
    out.panel.h = height - out.panel.y;
    out.ready = {out.panel.x + out.panel.w - 126 * s, out.panel.y + 10 * s, 112 * s, 30 * s};
    out.reroll = {out.ready.x - 138 * s, out.ready.y, 126 * s, 30 * s};
    out.moneyX = out.panel.x + 14 * s;
    out.moneyY = out.panel.y + 20 * s;
    return out;
}

inline game::ui::ClassicHudLayout computeLayout(const LayoutInput& in) {
    game::ui::ClassicHudLayoutInput hudIn;
    hudIn.uiW = in.uiW;
    hudIn.uiH = in.uiH;
    hudIn.shopCardsX = in.cardsX;
    hudIn.shopCardsY = in.cardsY;
    hudIn.shopCardsH = in.cardsH;
    hudIn.moneyTextW = in.moneyTextW;
    hudIn.moneyTextH = in.moneyTextH;
    hudIn.rerollTextW = in.rerollTextW;
    hudIn.rerollTextH = in.rerollTextH;
    hudIn.showReroll = in.showReroll;
    hudIn.iconVisible = false;
    return game::ui::computeClassicHudLayout(hudIn);
}

} // namespace game::runtime::ui_shop_hud


