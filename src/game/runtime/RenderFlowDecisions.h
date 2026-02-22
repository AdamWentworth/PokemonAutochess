#pragma once

#include "game/runtime/BackendRenderPolicy.h"

namespace game::runtime::render {

struct FrameRenderFlow {
    bool renderStateLayer = true;
    bool renderLegacyWorldLayer = false;
    bool renderLegacyHudLayer = false;
    bool renderBackendDebugLayer = false;
};

inline FrameRenderFlow decideFrameRenderFlow(const RenderRoutes& routes,
                                             bool renderWorldRequested) {
    FrameRenderFlow flow;
    if (!routes.hasRenderer) {
        return flow;
    }

    if (routes.usesLegacyRenderPath()) {
        flow.renderLegacyWorldLayer = renderWorldRequested;
        flow.renderLegacyHudLayer = renderWorldRequested;
        return flow;
    }

    flow.renderBackendDebugLayer =
        shouldRenderBackendDebugLayer(routes, renderWorldRequested);
    return flow;
}

} // namespace game::runtime::render
