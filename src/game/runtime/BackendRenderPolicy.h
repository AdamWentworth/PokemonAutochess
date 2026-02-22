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

} // namespace game::runtime::render
