#pragma once

namespace game::runtime::render {

// Canonical route selection for a running session frame.
// Ownership:
// - backend implementations hint preferred legacy/backend routes
// - GameSession materializes the active route
// - policy helpers consume this struct (instead of raw bool tuples)
struct RenderRoutes {
    bool hasRenderer = false;
    bool legacyRenderPath = false;
    bool legacyUiPath = false;

    bool usesLegacyRenderPath() const { return hasRenderer && legacyRenderPath; }
    bool usesBackendRenderPath() const { return hasRenderer && !legacyRenderPath; }
    bool usesLegacyUiPath() const { return hasRenderer && legacyUiPath; }
    bool usesBackendUiPath() const { return hasRenderer && !legacyUiPath; }
};

inline RenderRoutes makeRenderRoutes(bool hasRenderer,
                                     bool legacyRenderPath,
                                     bool legacyUiPath = false) {
    RenderRoutes routes;
    routes.hasRenderer = hasRenderer;
    routes.legacyRenderPath = legacyRenderPath;
    routes.legacyUiPath = legacyUiPath;
    return routes;
}

} // namespace game::runtime::render
