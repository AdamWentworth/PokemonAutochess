#pragma once

#include "game/runtime/routes/RenderRoutes.h"
#include "game/ui/ShopLayout.h"

namespace game::state::backend_ui {

inline bool shouldUseBackendUi(const game::runtime::render::RenderRoutes& routes,
                               bool preferLegacyUi = false) {
    if (preferLegacyUi) return false;
    return routes.usesBackendUiPath();
}

inline bool shouldRenderBackendTextMenu(const game::runtime::render::RenderRoutes& routes,
                                        bool isTextMenuMode) {
    // Text-menu rendering is backend-neutral (quads + lines only), so keep one
    // shared menu visual path across OpenGL and D3D12.
    return routes.hasRenderer && isTextMenuMode;
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
