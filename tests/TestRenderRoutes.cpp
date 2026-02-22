#include "game/runtime/RenderRoutes.h"

#include <string>

bool test_render_routes_contract(std::string& outFail) {
    using game::runtime::render::makeRenderRoutes;

    {
        const auto routes = makeRenderRoutes(false, true, true);
        if (routes.usesLegacyRenderPath() || routes.usesBackendRenderPath() ||
            routes.usesLegacyUiPath() || routes.usesBackendUiPath()) {
            outFail = "route helpers should all be false when renderer is unavailable";
            return false;
        }
    }

    {
        const auto routes = makeRenderRoutes(true, true, true);
        if (!routes.usesLegacyRenderPath() || routes.usesBackendRenderPath()) {
            outFail = "legacy render-path helpers mismatch";
            return false;
        }
        if (!routes.usesLegacyUiPath() || routes.usesBackendUiPath()) {
            outFail = "legacy UI-path helpers mismatch";
            return false;
        }
    }

    {
        const auto routes = makeRenderRoutes(true, false, false);
        if (routes.usesLegacyRenderPath() || !routes.usesBackendRenderPath()) {
            outFail = "backend render-path helpers mismatch";
            return false;
        }
        if (routes.usesLegacyUiPath() || !routes.usesBackendUiPath()) {
            outFail = "backend UI-path helpers mismatch";
            return false;
        }
    }

    {
        const auto routes = makeRenderRoutes(true, false, true);
        if (routes.usesLegacyRenderPath() || !routes.usesBackendRenderPath()) {
            outFail = "mixed-route render helpers mismatch";
            return false;
        }
        if (!routes.usesLegacyUiPath() || routes.usesBackendUiPath()) {
            outFail = "mixed-route UI helpers mismatch";
            return false;
        }
    }

    return true;
}
