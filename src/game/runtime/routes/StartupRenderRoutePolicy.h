#pragma once

#include "game/runtime/routes/RenderRoutes.h"

namespace game::runtime::render {

// Startup route policy for gameplay world/UI rendering.
// Legacy gameplay routes have been retired; all renderers use shared routes.
inline RenderRoutes selectStartupRenderRoutes(bool hasRenderer) {
    return makeRenderRoutes(hasRenderer);
}

} // namespace game::runtime::render
