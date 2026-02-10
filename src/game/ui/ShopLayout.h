#pragma once

namespace game::ui {

struct ShopRowLayout {
    int cardW = 136;
    int cardH = 94;
    int spacing = 14;
    int edgeMargin = 18;
};

struct ShopRowPlacement {
    int startX = 0;
    int y = 0;
    int totalWidth = 0;
};

struct ClassicHudLayoutInput {
    int uiW = 1280;
    int uiH = 720;
    int shopCardsX = 0;
    int shopCardsY = 0;
    int shopCardsH = 0;
    float moneyTextW = 0.0f;
    float moneyTextH = 0.0f;
    float rerollTextW = 0.0f;
    float rerollTextH = 0.0f;
    bool showReroll = false;
    bool iconVisible = false;
    float iconSize = 30.0f;
    float iconGap = 8.0f;
    float edgePadScale = 0.02f;
    float edgePadMin = 12.0f;
    float edgePadMax = 28.0f;
    float adjacentGap = 10.0f;
    float stackGap = 12.0f;
};

struct ClassicHudLayout {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float textX = 0.0f;
    float textY = 0.0f;
    float iconX = 0.0f;
    float iconY = 0.0f;
    float iconSize = 0.0f;
    float rerollX = 0.0f;
    float rerollY = 0.0f;
    float rerollW = 0.0f;
    float rerollH = 0.0f;
};

ShopRowLayout computeShopRowLayout(int uiW, int uiH, bool allItems);
ShopRowPlacement computeShopRowPlacement(int uiW, int uiH, int cardCount, const ShopRowLayout& layout);
ClassicHudLayout computeClassicHudLayout(const ClassicHudLayoutInput& in);

} // namespace game::ui
