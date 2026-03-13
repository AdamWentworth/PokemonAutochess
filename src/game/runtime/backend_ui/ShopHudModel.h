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


