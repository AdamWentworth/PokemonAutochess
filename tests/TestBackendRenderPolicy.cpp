#include "game/runtime/routes/BackendRenderPolicy.h"

#include <string>

bool test_backend_render_policy_contract(std::string& outFail) {
    using game::runtime::render::makeRenderRoutes;
    using game::runtime::render::shouldRenderBackendDebugLayer;
    using game::runtime::render::shouldRenderBackendWorldBackdrop;
    using game::runtime::render::shouldRenderWorldLayer;

    if (shouldRenderBackendDebugLayer(makeRenderRoutes(false), true)) {
        outFail = "backend debug layer should require render-enabled";
        return false;
    }
    if (shouldRenderBackendDebugLayer(makeRenderRoutes(true), false)) {
        outFail = "backend debug layer should be hidden for menu-only states";
        return false;
    }
    if (!shouldRenderBackendDebugLayer(makeRenderRoutes(true), true)) {
        outFail = "backend debug layer should render for backend world states";
        return false;
    }

    if (!shouldRenderBackendWorldBackdrop(makeRenderRoutes(true), true, false)) {
        outFail = "backend world backdrop should render in world states";
        return false;
    }
    if (shouldRenderBackendWorldBackdrop(makeRenderRoutes(true), false, false)) {
        outFail = "backend menu backdrop should be disabled unless explicitly allowed";
        return false;
    }
    if (!shouldRenderBackendWorldBackdrop(makeRenderRoutes(true), false, true)) {
        outFail = "backend menu backdrop allow flag should enable menu backdrop";
        return false;
    }

    if (shouldRenderWorldLayer(makeRenderRoutes(false), true, false)) {
        outFail = "world layer should require render-enabled";
        return false;
    }
    if (!shouldRenderWorldLayer(makeRenderRoutes(true), true, false)) {
        outFail = "world layer should render in world states";
        return false;
    }
    if (shouldRenderWorldLayer(makeRenderRoutes(true), false, false)) {
        outFail = "backend menu world layer should require explicit backdrop allow";
        return false;
    }
    if (!shouldRenderWorldLayer(makeRenderRoutes(true), false, true)) {
        outFail = "backend menu world layer should honor backdrop allow flag";
        return false;
    }

    return true;
}
