#include "game/runtime/BackendShopHudModel.h"

#include <string>

bool test_backend_shop_hud_model_contract(std::string& outFail) {
    using game::runtime::backend_shop_hud::LayoutInput;
    using game::runtime::backend_shop_hud::cardsAnchorH;
    using game::runtime::backend_shop_hud::cardsAnchorX;
    using game::runtime::backend_shop_hud::cardsAnchorY;
    using game::runtime::backend_shop_hud::computeLayout;
    using game::runtime::backend_shop_hud::interactionHint;
    using game::runtime::backend_shop_hud::keyboardPrefixedLabel;
    using game::runtime::backend_shop_hud::moneyLabel;
    using game::runtime::backend_shop_hud::rerollLabel;

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

