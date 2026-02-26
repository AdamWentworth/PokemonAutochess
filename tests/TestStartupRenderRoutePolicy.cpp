#include <string>

#include "game/runtime/routes/StartupRenderRoutePolicy.h"

bool test_startup_render_route_policy_contract(std::string& outFail) {
    using game::runtime::render::selectStartupRenderRoutes;

    {
        const auto routes = selectStartupRenderRoutes(
            false,
            true,
            true,
            true,
            true);
        if (routes.hasRenderer || routes.usesLegacyRenderPath() || routes.usesBackendRenderPath() ||
            routes.usesLegacyUiPath() || routes.usesBackendUiPath()) {
            outFail = "no-renderer startup routes should remain inactive";
            return false;
        }
    }

    {
        const auto routes = selectStartupRenderRoutes(
            true,
            true,
            true,
            true,
            false);
        if (!routes.hasRenderer || routes.usesLegacyRenderPath() || !routes.usesBackendRenderPath() ||
            routes.usesLegacyUiPath() || !routes.usesBackendUiPath()) {
            outFail = "OpenGL should default to shared gameplay routes without override";
            return false;
        }
    }

    {
        const auto routes = selectStartupRenderRoutes(
            true,
            true,
            true,
            true,
            true);
        if (!routes.hasRenderer || !routes.usesLegacyRenderPath() || routes.usesBackendRenderPath() ||
            !routes.usesLegacyUiPath() || routes.usesBackendUiPath()) {
            outFail = "OpenGL override should enable legacy gameplay routes";
            return false;
        }
    }

    {
        const auto routes = selectStartupRenderRoutes(
            true,
            true,
            true,
            false,
            true);
        if (!routes.hasRenderer || routes.usesLegacyRenderPath() || !routes.usesBackendRenderPath() ||
            routes.usesLegacyUiPath() || !routes.usesBackendUiPath()) {
            outFail = "legacy override should be ignored for non-OpenGL backends";
            return false;
        }
    }

    return true;
}
