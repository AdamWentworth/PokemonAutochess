#pragma once

#include "game/ui/ShopLayout.h"

namespace game::state::backend_ui {

inline bool shouldUseBackendUi(bool hasRenderer,
                               bool backendPrefersLegacyUiPath,
                               bool preferLegacyUi = false) {
    if (preferLegacyUi) return false;
    if (!hasRenderer) return false;
    return !backendPrefersLegacyUiPath;
}

inline bool shouldRenderBackendTextMenu(bool hasRenderer,
                                        bool isTextMenuMode) {
    // Text-menu rendering is backend-neutral (quads + lines only), so keep one
    // shared menu visual path across OpenGL and D3D12.
    return hasRenderer && isTextMenuMode;
}

inline bool shouldShowSellOverlay(bool isShopMode,
                                  bool hasWorld,
                                  bool unitDragActive,
                                  int dropZoneCardCount) {
    return isShopMode && hasWorld && unitDragActive && dropZoneCardCount > 0;
}

inline game::ui::SellDropZoneLayout computeSellOverlayOuterLayout(int uiW,
                                                                   int uiH,
                                                                   int dropZoneCardCount,
                                                                   bool useItemLayout) {
    return game::ui::computeSellDropZoneLayout(uiW, uiH, dropZoneCardCount, useItemLayout);
}

inline game::ui::SellDropZoneLayout computeSellOverlayHitLayout(const game::ui::SellDropZoneLayout& outer) {
    return game::ui::computeSellDropZoneCenterHitLayout(outer);
}

} // namespace game::state::backend_ui
