#include <string>

#include "game/runtime/routes/StartupRenderRoutePolicy.h"

bool test_startup_render_route_policy_contract(std::string& outFail) {
    using game::runtime::render::selectStartupRenderRoutes;

    {
        const auto routes = selectStartupRenderRoutes(false);
        if (routes.hasRenderer || routes.usesBackendRenderPath() || routes.usesBackendUiPath()) {
            outFail = "no-renderer startup routes should remain inactive";
            return false;
        }
    }

    {
        const auto routes = selectStartupRenderRoutes(true);
        if (!routes.hasRenderer || !routes.usesBackendRenderPath() || !routes.usesBackendUiPath()) {
            outFail = "all active renderers should use shared gameplay routes";
            return false;
        }
    }

    return true;
}
