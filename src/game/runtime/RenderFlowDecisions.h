#pragma once

#include "game/runtime/BackendRenderPolicy.h"

namespace game::runtime::render {

struct FrameRenderFlow {
    bool renderStateLayer = true;
    bool renderLegacyWorldLayer = false;
    bool renderLegacyHudLayer = false;
    bool renderBackendDebugLayer = false;
};

inline FrameRenderFlow decideFrameRenderFlow(bool renderEnabled,
                                             bool legacyRenderPath,
                                             bool renderWorldRequested) {
    FrameRenderFlow flow;
    if (!renderEnabled) {
        return flow;
    }

    if (legacyRenderPath) {
        flow.renderLegacyWorldLayer = renderWorldRequested;
        flow.renderLegacyHudLayer = renderWorldRequested;
        return flow;
    }

    flow.renderBackendDebugLayer =
        shouldRenderBackendDebugLayer(renderEnabled, legacyRenderPath, renderWorldRequested);
    return flow;
}

} // namespace game::runtime::render
