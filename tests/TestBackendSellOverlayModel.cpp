#include "game/runtime/BackendSellOverlayModel.h"
#include "game/ui/SellOverlayUiPolicy.h"

#include <string>

bool test_backend_sell_overlay_model_contract(std::string& outFail) {
    using game::runtime::backend_sell_overlay::buildModel;

    const auto hidden = buildModel(false, 1280, 720, 3, false, true);
    if (hidden.visible) {
        outFail = "hidden overlay should not be visible";
        return false;
    }

    const auto visibleMoney = buildModel(true, 1280, 720, 3, false, true);
    if (!visibleMoney.visible) {
        outFail = "visible overlay should be visible";
        return false;
    }
    if (visibleMoney.outer.w <= 0 || visibleMoney.outer.h <= 0) {
        outFail = "visible overlay should have positive outer bounds";
        return false;
    }
    if (visibleMoney.hit.w <= 0 || visibleMoney.hit.h <= 0) {
        outFail = "visible overlay should have positive hit bounds";
        return false;
    }
    if (visibleMoney.centerX <= 0.0f || visibleMoney.titleY <= 0.0f || visibleMoney.hintY <= 0.0f) {
        outFail = "overlay text anchors should be positive";
        return false;
    }
    const auto paidCopy = game::ui::sell_overlay::makeCopy(true);
    if (visibleMoney.copy.title != paidCopy.title) {
        outFail = "sell title mismatch expected '" + paidCopy.title + "' got '" + visibleMoney.copy.title + "'";
        return false;
    }
    if (visibleMoney.copy.hint != paidCopy.hint) {
        outFail = "sell hint mismatch for paid mode";
        return false;
    }

    const auto visibleNoMoney = buildModel(true, 1280, 720, 2, true, false);
    if (!visibleNoMoney.visible) {
        outFail = "item-layout overlay should still be visible";
        return false;
    }
    const auto freeCopy = game::ui::sell_overlay::makeCopy(false);
    if (visibleNoMoney.copy.hint != freeCopy.hint) {
        outFail = "sell hint mismatch for no-money mode";
        return false;
    }
    if (visibleNoMoney.outer.w <= 0 || visibleNoMoney.outer.h <= 0) {
        outFail = "item-layout overlay outer bounds invalid";
        return false;
    }

    return true;
}
