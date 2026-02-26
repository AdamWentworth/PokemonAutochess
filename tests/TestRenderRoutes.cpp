#include "game/runtime/routes/RenderRoutes.h"

#include <string>

bool test_render_routes_contract(std::string& outFail) {
    using game::runtime::render::makeRenderRoutes;

    {
        const auto routes = makeRenderRoutes(false);
        if (routes.usesBackendRenderPath() || routes.usesBackendUiPath()) {
            outFail = "route helpers should all be false when renderer is unavailable";
            return false;
        }
    }

    {
        const auto routes = makeRenderRoutes(true);
        if (!routes.usesBackendRenderPath() || !routes.usesBackendUiPath()) {
            outFail = "shared route helpers mismatch";
            return false;
        }
    }

    return true;
}
