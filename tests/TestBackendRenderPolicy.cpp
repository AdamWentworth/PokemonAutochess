#include "game/runtime/BackendRenderPolicy.h"

#include <string>

bool test_backend_render_policy_contract(std::string& outFail) {
    using game::runtime::render::shouldRenderBackendDebugLayer;
    using game::runtime::render::shouldRenderBackendWorldBackdrop;

    if (shouldRenderBackendDebugLayer(false, false, true)) {
        outFail = "backend debug layer should require render-enabled";
        return false;
    }
    if (shouldRenderBackendDebugLayer(true, true, true)) {
        outFail = "backend debug layer should be disabled for legacy path";
        return false;
    }
    if (shouldRenderBackendDebugLayer(true, false, false)) {
        outFail = "backend debug layer should be hidden for menu-only states";
        return false;
    }
    if (!shouldRenderBackendDebugLayer(true, false, true)) {
        outFail = "backend debug layer should render for backend world states";
        return false;
    }

    if (!shouldRenderBackendWorldBackdrop(true, false, false)) {
        outFail = "backend world backdrop should render in world states";
        return false;
    }
    if (!shouldRenderBackendWorldBackdrop(true, true, false)) {
        outFail = "world backdrop should still render for legacy world states";
        return false;
    }
    if (shouldRenderBackendWorldBackdrop(false, true, true)) {
        outFail = "legacy menu states should not render backend backdrop";
        return false;
    }
    if (shouldRenderBackendWorldBackdrop(false, false, false)) {
        outFail = "backend menu backdrop should be disabled unless explicitly allowed";
        return false;
    }
    if (!shouldRenderBackendWorldBackdrop(false, false, true)) {
        outFail = "backend menu backdrop allow flag should enable menu backdrop";
        return false;
    }

    return true;
}
