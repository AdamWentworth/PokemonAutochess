#include "game/runtime/ui/ShopHudModel.h"
#include "game/state/BackendShopSnapshot.h"

#include <string>
#include <utility>

bool test_ui_shop_hud_model_contract(std::string& outFail) {
    for (const auto size : {std::pair{640, 360}, std::pair{845, 513}, std::pair{1280, 720}, std::pair{1920, 1080}}) {
        const auto row = game::ui::computeShopRowLayout(size.first, size.second, false);
        const auto cards = game::ui::computeShopRowPlacement(size.first, size.second, 5, row);
        const auto dock = game::runtime::ui_shop_hud::computeDock(size.first, size.second,
                                                                  static_cast<float>(cards.startX), static_cast<float>(cards.y), static_cast<float>(cards.startX + cards.totalWidth));
        if (dock.ready.y + dock.ready.h >= cards.y || dock.ready.x + dock.ready.w > size.first ||
            dock.reroll.x + dock.reroll.w >= dock.ready.x || dock.panel.x < 0 ||
            dock.moneyX >= dock.reroll.x || dock.ready.y < size.second * .5f) {
            outFail = "Shop controls must stay together above the cards, inside the lower viewport and without overlaps.";
            return false;
        }
        using namespace game::state::backend_shop;
        auto entries = buildEntries({.shopMode = true, .mainCount = 5, .includeReroll = true, .includeReady = true});
        PlacementInput placement;
        placement.readyRect = {dock.ready.x, dock.ready.y, dock.ready.w, dock.ready.h};
        placement.hasReadyRect = true;
        applyPlacement(entries, placement);
        const auto *hit = findByPoint(entries, dock.ready.x + dock.ready.w * .5f, dock.ready.y + dock.ready.h * .5f);
        if (!hit || hit->action != ActionType::ShopReady || hit->keyboardSlot != 7) {
            outFail = "The moved Ready button must retain matching mouse and keyboard actions after resizing.";
            return false;
        }
    }
    using game::runtime::ui_shop_hud::LayoutInput;
    using game::runtime::ui_shop_hud::cardsAnchorH;
    using game::runtime::ui_shop_hud::cardsAnchorX;
    using game::runtime::ui_shop_hud::cardsAnchorY;
    using game::runtime::ui_shop_hud::computeLayout;
    using game::runtime::ui_shop_hud::interactionHint;
    using game::runtime::ui_shop_hud::keyboardPrefixedLabel;
    using game::runtime::ui_shop_hud::moneyLabel;
    using game::runtime::ui_shop_hud::rerollLabel;

    if (keyboardPrefixedLabel(1, "Ready") != "[1] Ready") {
        outFail = "keyboard label prefix mismatch";
        return false;
    }
    if (keyboardPrefixedLabel(10, "Ready") != "Ready") {
        outFail = "keyboard label should not prefix out-of-range slots";
        return false;
    }
    if (moneyLabel(17) != "Gold: 17") {
        outFail = "money label mismatch";
        return false;
    }
    if (moneyLabel(-5) != "Gold: 0") {
        outFail = "money label should clamp negatives";
        return false;
    }
    if (rerollLabel(3) != "[3] Reroll 2g") {
        outFail = "reroll label mismatch";
        return false;
    }
    if (interactionHint() != "Use mouse or keys 1-9") {
        outFail = "interaction hint mismatch";
        return false;
    }

    if (cardsAnchorX(18.4f) != 18) {
        outFail = "cards anchor x rounding mismatch";
        return false;
    }
    if (cardsAnchorY(100.0f, 720) != 600) {
        outFail = "cards anchor y should clamp to fallback row";
        return false;
    }
    if (cardsAnchorY(640.0f, 720) != 640) {
        outFail = "cards anchor y should allow lower rows";
        return false;
    }
    if (cardsAnchorH(0.0f) != 1) {
        outFail = "cards anchor h should clamp to positive";
        return false;
    }

    LayoutInput in;
    in.uiW = 1280;
    in.uiH = 720;
    in.cardsX = 50;
    in.cardsY = 600;
    in.cardsH = 96;
    in.moneyTextW = 110.0f;
    in.moneyTextH = 20.0f;
    in.rerollTextW = 120.0f;
    in.rerollTextH = 20.0f;
    in.showReroll = true;
    const auto hud = computeLayout(in);
    if (hud.textX <= 0.0f || hud.textY <= 0.0f) {
        outFail = "computed hud text position should be positive";
        return false;
    }
    if (hud.rerollX <= 0.0f || hud.rerollY <= 0.0f) {
        outFail = "computed hud reroll position should be positive when enabled";
        return false;
    }

    return true;
}




