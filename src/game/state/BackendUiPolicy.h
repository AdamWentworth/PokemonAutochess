#pragma once

#include "game/ui/ShopLayout.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace game::state::backend_ui {

inline bool shouldUseBackendUi(bool hasRenderer, const std::string& activeRendererBackend) {
    if (!hasRenderer) return false;
    std::string backend = activeRendererBackend;
    std::transform(
        backend.begin(),
        backend.end(),
        backend.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return backend != "opengl";
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
