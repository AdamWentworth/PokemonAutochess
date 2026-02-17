#pragma once

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

    // Non-legacy backends keep the backend debug gameplay layer active.
    // The layer itself decides whether to draw world geometry from renderWorldRequested.
    flow.renderBackendDebugLayer = true;
    return flow;
}

} // namespace game::runtime::render
