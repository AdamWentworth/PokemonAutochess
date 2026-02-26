#pragma once

#include "game/runtime/routes/RenderRoutes.h"

namespace game::runtime::render {

inline bool shouldRenderBackendDebugLayer(const RenderRoutes& routes,
                                          bool renderWorldRequested) {
    return routes.usesBackendRenderPath() && renderWorldRequested;
}

inline bool shouldRenderBackendWorldBackdrop(const RenderRoutes& routes,
                                             bool renderWorldRequested,
                                             bool allowMenuBackdrop) {
    if (renderWorldRequested) return true;
    return routes.usesBackendRenderPath() && allowMenuBackdrop;
}

inline bool shouldRenderWorldLayer(const RenderRoutes& routes,
                                   bool renderWorldRequested,
                                   bool allowBackendMenuBackdrop) {
    if (!routes.hasRenderer) return false;
    return shouldRenderBackendWorldBackdrop(routes, renderWorldRequested, allowBackendMenuBackdrop);
}

} // namespace game::runtime::render
