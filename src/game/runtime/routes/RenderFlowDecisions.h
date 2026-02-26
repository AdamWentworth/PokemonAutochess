#pragma once

#include "game/runtime/routes/BackendRenderPolicy.h"

namespace game::runtime::render {

struct FrameRenderFlow {
    bool renderStateLayer = true;
    bool renderWorldLayer = false;
};

inline FrameRenderFlow decideFrameRenderFlow(const RenderRoutes& routes,
                                             bool renderWorldRequested,
                                             bool allowBackendMenuBackdrop = false) {
    FrameRenderFlow flow;
    flow.renderWorldLayer =
        shouldRenderWorldLayer(routes, renderWorldRequested, allowBackendMenuBackdrop);
    return flow;
}

} // namespace game::runtime::render
