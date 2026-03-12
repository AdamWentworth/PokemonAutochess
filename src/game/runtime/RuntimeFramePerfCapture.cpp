#include "game/runtime/RuntimeFramePerfCapture.h"

#include <algorithm>

namespace game::runtime::frame_perf_capture {

BackendFrameOutputs resolveBackendFrameOutputs(const BackendFrameInputs& inputs) {
    BackendFrameOutputs out;
    if (inputs.hasBackendTimings && inputs.backendTimings.gpuFrameValid) {
        out.gpuFrameMs = std::max(0.0, static_cast<double>(inputs.backendTimings.gpuFrameMs));
        out.gpuFrameValid = true;
    }

    if (inputs.rendererHandlesPresentation) {
        if (inputs.hasBackendTimings) {
            out.presentWaitMs = std::max(0.0, static_cast<double>(inputs.backendTimings.presentWaitMs));
        }
    } else {
        out.presentWaitMs = std::max(0.0, inputs.measuredPresentWaitMs);
    }

    if (inputs.hasBackendStats) {
        out.drawCalls = inputs.backendStats.drawCalls;
        out.triangles = inputs.backendStats.triangles;
    }
    return out;
}

double computeSubmitMs(double submitRawMs, double presentWaitMs) {
    return std::max(0.0, submitRawMs - presentWaitMs);
}

double computeTotalPresentWaitMs(bool rendererHandlesPresentation,
                                 double beginFrameMs,
                                 double presentWaitMs) {
    return presentWaitMs + (rendererHandlesPresentation ? beginFrameMs : 0.0);
}

EngineRenderBuildBreakdown finalizeRenderBreakdown(double renderBuildMs,
                                                   float projectedUnitsMs,
                                                   const EngineRenderBuildBreakdown& rawBreakdown) {
    EngineRenderBuildBreakdown out = rawBreakdown;
    const float attributedMs =
        projectedUnitsMs +
        out.worldComposeMs +
        out.overlayPrepMs +
        out.worldBackgroundMs +
        out.worldTriangles3dMs +
        out.worldIndexedMs +
        out.worldDebugMs +
        out.spriteMs +
        out.uiMs;
    out.otherMs = std::max(0.0f, static_cast<float>(renderBuildMs) - attributedMs);
    return out;
}

} // namespace game::runtime::frame_perf_capture
