#pragma once

#include "game/ui/ShopLayout.h"

#include <string>

namespace game::state::backend_ui {

inline bool shouldUseBackendUi(bool hasRenderer,
                               const std::string& activeRendererBackend,
                               bool preferLegacyUi = false) {
    (void)activeRendererBackend;
    // UI flow parity target: once a renderer exists, use the shared backend UI path
    // for both OpenGL and D3D12 unless explicitly asked to keep legacy UI.
    if (preferLegacyUi) return false;
    if (!hasRenderer) return false;
    return true;
}

inline bool shouldRenderBackendTextMenu(bool hasRenderer,
                                        const std::string& activeRendererBackend,
                                        bool isTextMenuMode) {
    return isTextMenuMode && shouldUseBackendUi(hasRenderer, activeRendererBackend);
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
