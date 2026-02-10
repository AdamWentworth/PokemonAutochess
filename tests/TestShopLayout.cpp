#include <cmath>
#include <string>

#include "game/ui/ShopLayout.h"

static bool almostEq(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

bool test_shop_layout_invariants(std::string& outFail) {
    using game::ui::ClassicHudLayout;
    using game::ui::ClassicHudLayoutInput;
    using game::ui::SellDropZoneLayout;
    using game::ui::ShopRowLayout;
    using game::ui::ShopRowPlacement;
    using game::ui::computeClassicHudLayout;
    using game::ui::computeSellDropZoneLayout;
    using game::ui::computeShopRowLayout;
    using game::ui::computeShopRowPlacement;

    {
        const ShopRowLayout base = computeShopRowLayout(1280, 720, false);
        if (base.cardW != 136 || base.cardH != 94 || base.spacing != 14) {
            outFail = "Base shop layout (1280x720, pokemon cards) changed unexpectedly.";
            return false;
        }
        if (base.edgeMargin != 17) {
            outFail = "Base shop layout edge margin expected 17 at 1280x720.";
            return false;
        }
    }

    {
        const ShopRowLayout baseItems = computeShopRowLayout(1280, 720, true);
        if (baseItems.cardW != 88 || baseItems.cardH != 88 || baseItems.spacing != 12) {
            outFail = "Base shop layout (1280x720, item cards) changed unexpectedly.";
            return false;
        }
        if (baseItems.edgeMargin != 17) {
            outFail = "Base item layout edge margin expected 17 at 1280x720.";
            return false;
        }
    }

    {
        const ShopRowLayout low = computeShopRowLayout(320, 180, false);
        if (low.cardW != 82 || low.cardH != 56 || low.spacing != 8) {
            outFail = "Low-res clamp behavior for pokemon cards is incorrect.";
            return false;
        }
        if (low.edgeMargin != 10) {
            outFail = "Low-res edge margin should clamp to 10.";
            return false;
        }
    }

    {
        const ShopRowLayout high = computeShopRowLayout(3840, 2160, false);
        if (high.cardW != 245 || high.cardH != 169 || high.spacing != 25) {
            outFail = "High-res clamp behavior for pokemon cards is incorrect.";
            return false;
        }
        if (high.edgeMargin != 36) {
            outFail = "High-res edge margin should clamp to 36.";
            return false;
        }
    }

    {
        const ShopRowLayout highItems = computeShopRowLayout(3840, 2160, true);
        if (highItems.cardW != 158 || highItems.cardH != 158 || highItems.spacing != 22) {
            outFail = "High-res clamp behavior for item cards is incorrect.";
            return false;
        }
        if (highItems.edgeMargin != 36) {
            outFail = "High-res item edge margin should clamp to 36.";
            return false;
        }
    }

    {
        const ShopRowLayout layout = computeShopRowLayout(1280, 720, false);
        const ShopRowPlacement place = computeShopRowPlacement(1280, 720, 5, layout);
        if (place.totalWidth != 736) {
            outFail = "Shop row width is incorrect for 5 cards at base resolution.";
            return false;
        }
        if (place.startX != 272) {
            outFail = "Shop row X should be centered for 5 cards at base resolution.";
            return false;
        }
        if (place.y != 609) {
            outFail = "Shop row Y should anchor to bottom edge margin.";
            return false;
        }
    }

    {
        const ShopRowLayout layout = computeShopRowLayout(1280, 720, false);
        const ShopRowPlacement place = computeShopRowPlacement(1280, 720, 0, layout);
        if (place.totalWidth != 0 || place.startX != 640) {
            outFail = "Zero-card placement should produce zero width and center anchor.";
            return false;
        }
    }

    {
        const SellDropZoneLayout sell = computeSellDropZoneLayout(1280, 720, 5, false);
        if (sell.x != 135 || sell.y != 609 || sell.w != 125 || sell.h != 94) {
            outFail = "Sell zone placement changed unexpectedly for base shop layout.";
            return false;
        }
    }

    {
        const SellDropZoneLayout sell = computeSellDropZoneLayout(1280, 720, 8, false);
        if (sell.x != 17) {
            outFail = "Sell zone should clamp to left edge when shop row is very wide.";
            return false;
        }
    }

    {
        ClassicHudLayoutInput in;
        in.uiW = 1280;
        in.uiH = 720;
        in.shopCardsX = 272;
        in.shopCardsY = 609;
        in.shopCardsH = 94;
        in.moneyTextW = 24.0f;
        in.moneyTextH = 20.0f;
        in.rerollTextW = 90.0f;
        in.rerollTextH = 18.0f;
        in.showReroll = true;
        in.iconVisible = true;
        in.iconSize = 30.0f;
        in.iconGap = 8.0f;
        const ClassicHudLayout layout = computeClassicHudLayout(in);

        if (!almostEq(layout.x0, 172.0f) || !almostEq(layout.y0, 640.0f)) {
            outFail = "Classic HUD anchor placement changed unexpectedly.";
            return false;
        }
        if (!almostEq(layout.textX, 210.0f) || !almostEq(layout.textY, 640.0f)) {
            outFail = "Classic HUD text anchor changed unexpectedly.";
            return false;
        }
        if (!almostEq(layout.rerollY, 685.0f) || !almostEq(layout.rerollH, 18.0f)) {
            outFail = "Classic HUD reroll placement changed unexpectedly.";
            return false;
        }
    }

    {
        ClassicHudLayoutInput in;
        in.uiW = 1280;
        in.uiH = 720;
        in.shopCardsX = 0;
        in.shopCardsY = 609;
        in.shopCardsH = 94;
        in.moneyTextW = 80.0f;
        in.moneyTextH = 18.0f;
        in.rerollTextW = 140.0f;
        in.rerollTextH = 18.0f;
        in.showReroll = true;
        in.iconVisible = false;
        const ClassicHudLayout layout = computeClassicHudLayout(in);

        // Left clamp should respect edge pad at 1280x720 (14px).
        if (!almostEq(layout.x0, 14.0f)) {
            outFail = "Classic HUD should clamp to left edge padding.";
            return false;
        }
    }

    {
        ClassicHudLayoutInput in;
        in.uiW = 1280;
        in.uiH = 720;
        in.shopCardsX = 1000;
        in.shopCardsY = 609;
        in.shopCardsH = 94;
        in.moneyTextW = 70.0f;
        in.moneyTextH = 18.0f;
        in.showReroll = false;
        in.iconVisible = false;
        const ClassicHudLayout layout = computeClassicHudLayout(in);

        if (!almostEq(layout.rerollW, 0.0f) || !almostEq(layout.rerollH, 0.0f)) {
            outFail = "Classic HUD should clear reroll rect when reroll is hidden.";
            return false;
        }
        if (!almostEq(layout.rerollY, 703.0f)) {
            outFail = "Classic HUD reroll baseline should remain card-bottom aligned.";
            return false;
        }
    }

    return true;
}
