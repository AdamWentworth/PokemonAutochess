#pragma once

#include "game/runtime/BackendRenderPolicy.h"

namespace game::runtime::render {

struct FrameRenderFlow {
    bool renderStateLayer = true;
    bool renderWorldLayer = false;
    bool renderLegacyHudLayer = false;
};

inline FrameRenderFlow decideFrameRenderFlow(const RenderRoutes& routes,
                                             bool renderWorldRequested,
                                             bool allowBackendMenuBackdrop = false) {
    FrameRenderFlow flow;
    flow.renderWorldLayer =
        shouldRenderWorldLayer(routes, renderWorldRequested, allowBackendMenuBackdrop);
    flow.renderLegacyHudLayer = shouldRenderLegacyHudLayer(routes, renderWorldRequested);
    return flow;
}

} // namespace game::runtime::render
