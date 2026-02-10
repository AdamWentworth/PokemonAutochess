#include "game/ui/ShopLayout.h"

#include <algorithm>
#include <cmath>

namespace game::ui {

ShopRowLayout computeShopRowLayout(int uiW, int uiH, bool allItems) {
    const float scale = std::clamp(
        std::min(static_cast<float>(uiW) / 1280.0f,
                 static_cast<float>(uiH) / 720.0f),
        0.60f, 1.80f);

    const int baseW = allItems ? 88 : 136;
    const int baseH = allItems ? 88 : 94;
    const int baseSpacing = allItems ? 12 : 14;

    ShopRowLayout out;
    out.cardW = std::max(56, static_cast<int>(std::round(static_cast<float>(baseW) * scale)));
    out.cardH = std::max(56, static_cast<int>(std::round(static_cast<float>(baseH) * scale)));
    out.spacing = std::max(6, static_cast<int>(std::round(static_cast<float>(baseSpacing) * scale)));
    out.edgeMargin = std::clamp(
        static_cast<int>(std::round(std::min(uiW, uiH) * 0.024f)),
        10, 36);
    return out;
}

ShopRowPlacement computeShopRowPlacement(int uiW, int uiH, int cardCount, const ShopRowLayout& layout) {
    ShopRowPlacement out;
    const int count = std::max(0, cardCount);
    out.totalWidth = (count > 0) ? (count * layout.cardW + (count - 1) * layout.spacing) : 0;
    out.startX = (uiW - out.totalWidth) / 2;
    out.y = std::max(0, uiH - layout.cardH - layout.edgeMargin);
    return out;
}

SellDropZoneLayout computeSellDropZoneLayout(int uiW, int uiH, int cardCount, bool allItems) {
    SellDropZoneLayout out;
    if (cardCount <= 0 || uiW <= 0 || uiH <= 0) return out;

    const ShopRowLayout row = computeShopRowLayout(uiW, uiH, allItems);
    const ShopRowPlacement place = computeShopRowPlacement(uiW, uiH, cardCount, row);

    const float zoneW = std::clamp(static_cast<float>(row.cardW) * 0.92f, 76.0f, 150.0f);
    const float zoneH = std::clamp(static_cast<float>(row.cardH), 56.0f, 128.0f);
    const float gap = std::clamp(static_cast<float>(row.spacing) * 0.85f, 8.0f, 18.0f);

    const float minX = static_cast<float>(row.edgeMargin);
    const float maxX = std::max(minX, static_cast<float>(uiW) - zoneW - static_cast<float>(row.edgeMargin));
    const float rawX = static_cast<float>(place.startX) - gap - zoneW;
    const float rawY = static_cast<float>(place.y);

    out.x = static_cast<int>(std::round(std::clamp(rawX, minX, maxX)));
    out.y = static_cast<int>(std::round(std::clamp(rawY, 0.0f, static_cast<float>(uiH))));
    out.w = static_cast<int>(std::round(zoneW));
    out.h = static_cast<int>(std::round(zoneH));
    return out;
}

ClassicHudLayout computeClassicHudLayout(const ClassicHudLayoutInput& in) {
    ClassicHudLayout out;

    const float iconWidth = in.iconVisible ? (in.iconSize + in.iconGap) : 0.0f;
    const float topRowW = in.moneyTextW + iconWidth;
    const float topRowH = std::max(in.moneyTextH, in.iconVisible ? in.iconSize : 0.0f);
    const float rerollW = in.showReroll ? in.rerollTextW : 0.0f;
    const float rerollH = in.showReroll ? in.rerollTextH : 0.0f;
    const float blockW = std::max(topRowW, rerollW);

    const float edgePad = std::clamp(
        std::round(static_cast<float>(std::min(in.uiW, in.uiH)) * in.edgePadScale),
        in.edgePadMin, in.edgePadMax);
    const float maxX = std::max(edgePad, static_cast<float>(in.uiW) - blockW - edgePad);
    const float desiredX = static_cast<float>(in.shopCardsX) - blockW - in.adjacentGap;
    const float x0 = std::clamp(desiredX, edgePad, maxX);

    const float cardBottom = static_cast<float>(in.shopCardsY + in.shopCardsH);
    const float y0 = in.showReroll
        ? (cardBottom - rerollH - in.stackGap - topRowH)
        : (cardBottom - topRowH);

    out.x0 = x0;
    out.y0 = y0 + in.topRowOffsetY;
    out.textX = x0 + iconWidth;
    out.textY = y0 + in.topRowOffsetY;
    out.iconX = x0;
    out.iconY = y0 + in.topRowOffsetY;
    out.iconSize = in.iconVisible ? in.iconSize : 0.0f;
    out.rerollX = x0;
    out.rerollY = cardBottom - rerollH;
    out.rerollW = rerollW;
    out.rerollH = rerollH;
    return out;
}

} // namespace game::ui
