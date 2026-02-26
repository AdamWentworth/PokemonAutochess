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
    if (routes.usesLegacyRenderPath()) return false;
    return routes.usesBackendRenderPath() && allowMenuBackdrop;
}

inline bool shouldRenderWorldLayer(const RenderRoutes& routes,
                                   bool renderWorldRequested,
                                   bool allowBackendMenuBackdrop) {
    if (!routes.hasRenderer) return false;
    if (routes.usesLegacyRenderPath()) return renderWorldRequested;
    return shouldRenderBackendWorldBackdrop(routes, renderWorldRequested, allowBackendMenuBackdrop);
}

inline bool shouldRenderLegacyHudLayer(const RenderRoutes& routes,
                                       bool renderWorldRequested) {
    return routes.usesLegacyRenderPath() && renderWorldRequested;
}

} // namespace game::runtime::render
