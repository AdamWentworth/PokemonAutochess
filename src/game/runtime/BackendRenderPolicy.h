#pragma once

#include "game/runtime/RenderRoutes.h"

namespace game::runtime::render {

inline bool shouldRenderBackendDebugLayer(const RenderRoutes& routes,
                                          bool renderWorldRequested) {
    return routes.usesBackendRenderPath() && renderWorldRequested;
}

inline bool shouldRenderBackendWorldBackdrop(const RenderRoutes& routes,
                                             bool renderWorldRequested,
                                             bool allowMenuBackdrop) {
    if (renderWorldRequested) return true;
    if (routes.usesLegacyRenderPath()) return false;
    return routes.usesBackendRenderPath() && allowMenuBackdrop;
}

// Transitional overloads for existing call sites.
inline bool shouldRenderBackendDebugLayer(bool renderEnabled,
                                          bool legacyRenderPath,
                                          bool renderWorldRequested) {
    return shouldRenderBackendDebugLayer(
        makeRenderRoutes(renderEnabled, legacyRenderPath),
        renderWorldRequested);
}

inline bool shouldRenderBackendWorldBackdrop(bool renderWorldRequested,
                                             bool legacyRenderPath,
                                             bool allowMenuBackdrop) {
    return shouldRenderBackendWorldBackdrop(
        makeRenderRoutes(true, legacyRenderPath),
        renderWorldRequested,
        allowMenuBackdrop);
}

} // namespace game::runtime::render
