#pragma once

namespace game::runtime::render {

// Canonical route selection for a running session frame.
// Ownership:
// - backend implementations hint preferred legacy/backend routes
// - GameSession materializes the active route
// - policy helpers consume this struct (instead of raw bool tuples)
struct RenderRoutes {
    bool hasRenderer = false;

    bool usesBackendRenderPath() const { return hasRenderer; }
    bool usesBackendUiPath() const { return hasRenderer; }
};

inline RenderRoutes makeRenderRoutes(bool hasRenderer) {
    RenderRoutes routes;
    routes.hasRenderer = hasRenderer;
    return routes;
}

} // namespace game::runtime::render
