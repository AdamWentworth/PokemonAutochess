#pragma once

#include <string>

namespace game::ui::sell_overlay {

struct Copy {
    std::string title;
    std::string hint;
    float titleScale = 1.0f;
    float hintScale = 0.78f;
};

inline Copy makeCopy(bool paysMoney) {
    Copy out;
    out.title = paysMoney ? "[ SELL ]" : "[ RELEASE ]";
    out.hint = paysMoney ? "Drop unit in center for gold"
                         : "Drop unit in center to release";
    return out;
}

inline bool shouldRenderShopCards(bool showSellOverlay) {
    return !showSellOverlay;
}

inline bool shouldRenderItemRow(bool hasShopItems, bool showSellOverlay) {
    return hasShopItems && !showSellOverlay;
}

} // namespace game::ui::sell_overlay
