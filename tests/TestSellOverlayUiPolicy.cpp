#include "game/ui/SellOverlayUiPolicy.h"

#include <string>

bool test_sell_overlay_ui_policy_contract(std::string& outFail) {
    using game::ui::sell_overlay::Copy;
    using game::ui::sell_overlay::makeCopy;
    using game::ui::sell_overlay::shouldRenderItemRow;
    using game::ui::sell_overlay::shouldRenderShopCards;

    if (shouldRenderShopCards(true)) {
        outFail = "shop cards should not render when sell overlay is active";
        return false;
    }
    if (!shouldRenderShopCards(false)) {
        outFail = "shop cards should render when sell overlay is inactive";
        return false;
    }

    if (!shouldRenderItemRow(true, false)) {
        outFail = "item row should render when available and overlay is inactive";
        return false;
    }
    if (shouldRenderItemRow(true, true)) {
        outFail = "item row should hide while sell overlay is active";
        return false;
    }
    if (shouldRenderItemRow(false, false)) {
        outFail = "item row should not render when there are no item cards";
        return false;
    }

    {
        const Copy copy = makeCopy(true);
        if (copy.title != "[ SELL ]") {
            outFail = "sell copy title mismatch";
            return false;
        }
        if (copy.hint != "Drop unit in center for gold") {
            outFail = "sell copy hint mismatch";
            return false;
        }
        if (copy.titleScale != 1.0f || copy.hintScale != 0.78f) {
            outFail = "sell copy scale contract mismatch";
            return false;
        }
    }

    {
        const Copy copy = makeCopy(false);
        if (copy.title != "[ RELEASE ]") {
            outFail = "release copy title mismatch";
            return false;
        }
        if (copy.hint != "Drop unit in center to release") {
            outFail = "release copy hint mismatch";
            return false;
        }
    }

    return true;
}
