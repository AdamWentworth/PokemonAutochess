#pragma once

#include "engine/core/EngineServices.h"
#include "game/runtime/loop/RuntimePerfAccumulator.h"

namespace game::runtime::frame_observation {

struct ServiceSnapshot {
    std::uint32_t visibleAnimatedUnits = 0u;
    std::uint32_t particleCount = 0u;
    float projectedUnitsMs = 0.0f;
    float projectedPoseEvalMs = 0.0f;
    float projectedModelMs = 0.0f;
    float projectedModelPrepMs = 0.0f;
    float projectedModelGeometryMs = 0.0f;
    float projectedOverlayMs = 0.0f;
    std::uint32_t projectedUnitsProcessed = 0u;
    std::uint32_t projectedModelUnits = 0u;
    std::uint32_t projectedClipSkinnedUnits = 0u;
    std::uint32_t projectedSharedRigidBatches = 0u;
    std::uint32_t projectedGpuClipSkinBatches = 0u;
    std::uint32_t projectedGpuClipPaletteBatches = 0u;
    std::uint32_t projectedCpuRewriteBatches = 0u;
    std::uint32_t projectedIndexedBatchesQueued = 0u;
    EngineRenderBuildBreakdown rawRenderBreakdown{};
};

struct SampleInputs {
    double frameDt = 0.0;
    double frameCpuMs = 0.0;
    double fixedMs = 0.0;
    double fixedTickWorkMs = 0.0;
    double renderBuildMs = 0.0;
    double renderSubmitMs = 0.0;
    double presentWaitMs = 0.0;
    double legacyRenderMs = 0.0;
    double legacySwapMs = 0.0;
    double gpuFrameMs = 0.0;
    bool gpuFrameValid = false;
    std::uint32_t drawCalls = 0u;
    std::uint64_t triangles = 0u;
    std::uint32_t indexedOpaqueDraws = 0u;
    std::uint32_t indexedBlendDraws = 0u;
    std::uint32_t indexedCachedDraws = 0u;
    std::uint32_t indexedDynamicDraws = 0u;
    std::uint32_t indexedInstancedDraws = 0u;
    std::uint32_t indexedOutlineBatches = 0u;
    std::uint32_t indexedGeometrySwitches = 0u;
    std::uint32_t indexedMaterialSwitches = 0u;
    std::uint32_t indexedTextureSwitches = 0u;
    std::uint32_t indexedGlTextureBindCalls = 0u;
    std::uint32_t indexedD3d12PsoSets = 0u;
    std::uint32_t indexedD3d12DescriptorTableSets = 0u;
    std::uint32_t fastSceneInstances = 0u;
    std::uint32_t fastSceneDrawClasses = 0u;
    std::uint32_t fastSceneVisibleSkeletons = 0u;
    std::uint64_t fastScenePaletteUploadBytes = 0u;
    std::uint32_t fastSceneMaterialTableBinds = 0u;
    std::uint32_t fastSceneIndirectCommands = 0u;
    EngineFixedPerfBreakdown fixedBreakdown{};
    int fixedTicks = 0;
    int fixedTicksDropped = 0;
};

ServiceSnapshot captureServiceSnapshot(const EngineServices& services);

game::runtime::perf_accum::FrameSample makePerfSample(const SampleInputs& inputs,
                                                      const ServiceSnapshot& snapshot);

} // namespace game::runtime::frame_observation

