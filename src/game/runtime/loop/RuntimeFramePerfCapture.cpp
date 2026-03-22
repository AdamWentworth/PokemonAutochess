#include "game/runtime/loop/RuntimeFramePerfCapture.h"

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
        out.indexedOpaqueDraws = inputs.backendStats.indexedOpaqueDraws;
        out.indexedBlendDraws = inputs.backendStats.indexedBlendDraws;
        out.indexedCachedDraws = inputs.backendStats.indexedCachedDraws;
        out.indexedDynamicDraws = inputs.backendStats.indexedDynamicDraws;
        out.indexedInstancedDraws = inputs.backendStats.indexedInstancedDraws;
        out.indexedOutlineBatches = inputs.backendStats.indexedOutlineBatches;
        out.indexedGeometrySwitches = inputs.backendStats.indexedGeometrySwitches;
        out.indexedMaterialSwitches = inputs.backendStats.indexedMaterialSwitches;
        out.indexedTextureSwitches = inputs.backendStats.indexedTextureSwitches;
        out.indexedGlTextureBindCalls = inputs.backendStats.indexedGlTextureBindCalls;
        out.indexedD3d12PsoSets = inputs.backendStats.indexedD3d12PsoSets;
        out.indexedD3d12DescriptorTableSets =
            inputs.backendStats.indexedD3d12DescriptorTableSets;
        out.fastSceneInstances = inputs.backendStats.fastSceneInstances;
        out.fastSceneDrawClasses = inputs.backendStats.fastSceneDrawClasses;
        out.fastSceneVisibleSkeletons = inputs.backendStats.fastSceneVisibleSkeletons;
        out.fastScenePaletteUploadBytes =
            inputs.backendStats.fastScenePaletteUploadBytes;
        out.fastSceneMaterialTableBinds =
            inputs.backendStats.fastSceneMaterialTableBinds;
        out.fastSceneIndirectCommands = inputs.backendStats.fastSceneIndirectCommands;
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

