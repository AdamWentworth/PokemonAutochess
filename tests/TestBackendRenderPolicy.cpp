#include "game/runtime/routes/BackendRenderPolicy.h"

#include <string>

bool test_backend_render_policy_contract(std::string& outFail) {
    using game::runtime::render::makeRenderRoutes;
    using game::runtime::render::shouldRenderBackendDebugLayer;
    using game::runtime::render::shouldRenderBackendWorldBackdrop;
    using game::runtime::render::shouldRenderLegacyHudLayer;
    using game::runtime::render::shouldRenderWorldLayer;

    if (shouldRenderBackendDebugLayer(makeRenderRoutes(false, false), true)) {
        outFail = "backend debug layer should require render-enabled";
        return false;
    }
    if (shouldRenderBackendDebugLayer(makeRenderRoutes(true, true), true)) {
        outFail = "backend debug layer should be disabled for legacy path";
        return false;
    }
    if (shouldRenderBackendDebugLayer(makeRenderRoutes(true, false), false)) {
        outFail = "backend debug layer should be hidden for menu-only states";
        return false;
    }
    if (!shouldRenderBackendDebugLayer(makeRenderRoutes(true, false), true)) {
        outFail = "backend debug layer should render for backend world states";
        return false;
    }

    if (!shouldRenderBackendWorldBackdrop(makeRenderRoutes(true, false), true, false)) {
        outFail = "backend world backdrop should render in world states";
        return false;
    }
    if (!shouldRenderBackendWorldBackdrop(makeRenderRoutes(true, true), true, false)) {
        outFail = "world backdrop should still render for legacy world states";
        return false;
    }
    if (shouldRenderBackendWorldBackdrop(makeRenderRoutes(true, true), false, true)) {
        outFail = "legacy menu states should not render backend backdrop";
        return false;
    }
    if (shouldRenderBackendWorldBackdrop(makeRenderRoutes(true, false), false, false)) {
        outFail = "backend menu backdrop should be disabled unless explicitly allowed";
        return false;
    }
    if (!shouldRenderBackendWorldBackdrop(makeRenderRoutes(true, false), false, true)) {
        outFail = "backend menu backdrop allow flag should enable menu backdrop";
        return false;
    }

    if (shouldRenderWorldLayer(makeRenderRoutes(false, false), true, false)) {
        outFail = "world layer should require render-enabled";
        return false;
    }
    if (!shouldRenderWorldLayer(makeRenderRoutes(true, true), true, false)) {
        outFail = "legacy world layer should render in world states";
        return false;
    }
    if (shouldRenderWorldLayer(makeRenderRoutes(true, true), false, true)) {
        outFail = "legacy menu world layer should remain hidden";
        return false;
    }
    if (shouldRenderWorldLayer(makeRenderRoutes(true, false), false, false)) {
        outFail = "backend menu world layer should require explicit backdrop allow";
        return false;
    }
    if (!shouldRenderWorldLayer(makeRenderRoutes(true, false), false, true)) {
        outFail = "backend menu world layer should honor backdrop allow flag";
        return false;
    }

    if (shouldRenderLegacyHudLayer(makeRenderRoutes(false, true), true)) {
        outFail = "legacy HUD layer should require render-enabled";
        return false;
    }
    if (!shouldRenderLegacyHudLayer(makeRenderRoutes(true, true), true)) {
        outFail = "legacy HUD layer should render in legacy world states";
        return false;
    }
    if (shouldRenderLegacyHudLayer(makeRenderRoutes(true, true), false)) {
        outFail = "legacy HUD layer should hide in menu-only states";
        return false;
    }
    if (shouldRenderLegacyHudLayer(makeRenderRoutes(true, false), true)) {
        outFail = "legacy HUD layer should not render on backend route";
        return false;
    }

    return true;
}
