#include "game/runtime/loop/RuntimeFrameObservation.h"

#include "game/runtime/loop/RuntimeFramePerfCapture.h"

namespace game::runtime::frame_observation {

ServiceSnapshot captureServiceSnapshot(const EngineServices& services) {
    ServiceSnapshot out;
    out.visibleAnimatedUnits = services.frameVisibleAnimatedUnits;
    out.particleCount = services.frameParticleCount;
    out.projectedUnitsMs = services.frameProjectedUnitsMs;
    out.projectedPoseEvalMs = services.frameProjectedPoseEvalMs;
    out.projectedModelMs = services.frameProjectedModelMs;
    out.projectedModelPrepMs = services.frameProjectedModelPrepMs;
    out.projectedModelGeometryMs = services.frameProjectedModelGeometryMs;
    out.projectedOverlayMs = services.frameProjectedOverlayMs;
    out.projectedUnitsProcessed = services.frameProjectedUnitsProcessed;
    out.projectedModelUnits = services.frameProjectedModelUnits;
    out.projectedClipSkinnedUnits = services.frameProjectedClipSkinnedUnits;
    out.projectedSharedRigidBatches = services.frameProjectedSharedRigidBatches;
    out.projectedGpuClipSkinBatches = services.frameProjectedGpuClipSkinBatches;
    out.projectedGpuClipPaletteBatches = services.frameProjectedGpuClipPaletteBatches;
    out.projectedCpuRewriteBatches = services.frameProjectedCpuRewriteBatches;
    out.projectedIndexedBatchesQueued = services.frameProjectedIndexedBatchesQueued;
    out.rawRenderBreakdown = services.frameRenderBuildBreakdown;
    return out;
}

game::runtime::perf_accum::FrameSample makePerfSample(const SampleInputs& inputs,
                                                      const ServiceSnapshot& snapshot) {
    game::runtime::perf_accum::FrameSample sample;
    sample.frameDt = inputs.frameDt;
    sample.frameCpuMs = inputs.frameCpuMs;
    sample.fixedMs = inputs.fixedMs;
    sample.fixedTickWorkMs = inputs.fixedTickWorkMs;
    sample.renderBuildMs = inputs.renderBuildMs;
    sample.renderSubmitMs = inputs.renderSubmitMs;
    sample.presentWaitMs = inputs.presentWaitMs;
    sample.legacyRenderMs = inputs.legacyRenderMs;
    sample.legacySwapMs = inputs.legacySwapMs;
    sample.gpuFrameMs = inputs.gpuFrameMs;
    sample.gpuFrameValid = inputs.gpuFrameValid;
    sample.drawCalls = inputs.drawCalls;
    sample.triangles = inputs.triangles;
    sample.indexedOpaqueDraws = inputs.indexedOpaqueDraws;
    sample.indexedBlendDraws = inputs.indexedBlendDraws;
    sample.indexedCachedDraws = inputs.indexedCachedDraws;
    sample.indexedDynamicDraws = inputs.indexedDynamicDraws;
    sample.indexedInstancedDraws = inputs.indexedInstancedDraws;
    sample.indexedOutlineBatches = inputs.indexedOutlineBatches;
    sample.indexedGeometrySwitches = inputs.indexedGeometrySwitches;
    sample.indexedMaterialSwitches = inputs.indexedMaterialSwitches;
    sample.indexedTextureSwitches = inputs.indexedTextureSwitches;
    sample.indexedGlTextureBindCalls = inputs.indexedGlTextureBindCalls;
    sample.indexedD3d12PsoSets = inputs.indexedD3d12PsoSets;
    sample.indexedD3d12DescriptorTableSets = inputs.indexedD3d12DescriptorTableSets;
    sample.visibleAnimatedUnits = snapshot.visibleAnimatedUnits;
    sample.particleCount = snapshot.particleCount;
    sample.projectedUnitsMs = snapshot.projectedUnitsMs;
    sample.projectedPoseEvalMs = snapshot.projectedPoseEvalMs;
    sample.projectedModelMs = snapshot.projectedModelMs;
    sample.projectedModelPrepMs = snapshot.projectedModelPrepMs;
    sample.projectedModelGeometryMs = snapshot.projectedModelGeometryMs;
    sample.projectedOverlayMs = snapshot.projectedOverlayMs;
    sample.projectedUnitsProcessed = snapshot.projectedUnitsProcessed;
    sample.projectedModelUnits = snapshot.projectedModelUnits;
    sample.projectedClipSkinnedUnits = snapshot.projectedClipSkinnedUnits;
    sample.projectedSharedRigidBatches = snapshot.projectedSharedRigidBatches;
    sample.projectedGpuClipSkinBatches = snapshot.projectedGpuClipSkinBatches;
    sample.projectedGpuClipPaletteBatches = snapshot.projectedGpuClipPaletteBatches;
    sample.projectedCpuRewriteBatches = snapshot.projectedCpuRewriteBatches;
    sample.projectedIndexedBatchesQueued = snapshot.projectedIndexedBatchesQueued;
    sample.renderBreakdown = game::runtime::frame_perf_capture::finalizeRenderBreakdown(
        inputs.renderBuildMs,
        snapshot.projectedUnitsMs,
        snapshot.rawRenderBreakdown);
    sample.fixedBreakdown = inputs.fixedBreakdown;
    sample.fixedTicks = inputs.fixedTicks;
    sample.fixedTicksDropped = inputs.fixedTicksDropped;
    return sample;
}

} // namespace game::runtime::frame_observation

