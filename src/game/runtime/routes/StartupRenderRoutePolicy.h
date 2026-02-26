#pragma once

#include "game/runtime/routes/RenderRoutes.h"

namespace game::runtime::render {

// Startup route policy for gameplay world/UI rendering.
// Shared routes are default for all active renderers.
// Legacy gameplay routes are only re-enabled by an explicit dev override,
// and only when the active renderer backend is OpenGL.
inline RenderRoutes selectStartupRenderRoutes(bool hasRenderer,
                                              bool backendPrefersLegacyRenderPath,
                                              bool backendPrefersLegacyUiPath,
                                              bool isOpenGlBackend,
                                              bool allowLegacyGameplayOverride) {
    RenderRoutes routes = makeRenderRoutes(hasRenderer, false, false);
    if (!hasRenderer) return routes;

    if (allowLegacyGameplayOverride && isOpenGlBackend) {
        routes.legacyRenderPath = backendPrefersLegacyRenderPath;
        routes.legacyUiPath = backendPrefersLegacyUiPath;
    }

    return routes;
}

} // namespace game::runtime::render
