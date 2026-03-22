#pragma once

#include <cstdint>

#include "engine/core/EngineServices.h"

namespace game::runtime::perf_accum {

struct FrameSample {
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
    EngineRenderBuildBreakdown renderBreakdown{};
    EngineFixedPerfBreakdown fixedBreakdown{};
    int fixedTicks = 0;
    int fixedTicksDropped = 0;
};

struct WindowSummary {
    EngineFramePerfStats framePerf{};
};

class RollingAccumulator {
public:
    void addFrame(const FrameSample& sample);
    bool readyToEmit() const;
    WindowSummary makeSummaryAndReset();

private:
    int frameCount_ = 0;
    double fpsTimer_ = 0.0;
    double frameMs_ = 0.0;
    double fixedMs_ = 0.0;
    double fixedTickWorkMs_ = 0.0;
    double renderBuildMs_ = 0.0;
    double renderSubmitMs_ = 0.0;
    double presentWaitMs_ = 0.0;
    double legacyRenderMs_ = 0.0;
    double legacySwapMs_ = 0.0;
    double gpuFrameMs_ = 0.0;
    int gpuFrameSamples_ = 0;
    double drawCalls_ = 0.0;
    double triangles_ = 0.0;
    double indexedOpaqueDraws_ = 0.0;
    double indexedBlendDraws_ = 0.0;
    double indexedCachedDraws_ = 0.0;
    double indexedDynamicDraws_ = 0.0;
    double indexedInstancedDraws_ = 0.0;
    double indexedOutlineBatches_ = 0.0;
    double indexedGeometrySwitches_ = 0.0;
    double indexedMaterialSwitches_ = 0.0;
    double indexedTextureSwitches_ = 0.0;
    double indexedGlTextureBindCalls_ = 0.0;
    double indexedD3d12PsoSets_ = 0.0;
    double indexedD3d12DescriptorTableSets_ = 0.0;
    double fastSceneInstances_ = 0.0;
    double fastSceneDrawClasses_ = 0.0;
    double fastSceneVisibleSkeletons_ = 0.0;
    double fastScenePaletteUploadBytes_ = 0.0;
    double fastSceneMaterialTableBinds_ = 0.0;
    double fastSceneIndirectCommands_ = 0.0;
    double visibleAnimatedUnits_ = 0.0;
    double particleCount_ = 0.0;
    double projectedUnitsMs_ = 0.0;
    double projectedPoseEvalMs_ = 0.0;
    double projectedModelMs_ = 0.0;
    double projectedModelPrepMs_ = 0.0;
    double projectedModelGeometryMs_ = 0.0;
    double projectedOverlayMs_ = 0.0;
    double projectedUnitsProcessed_ = 0.0;
    double projectedModelUnits_ = 0.0;
    double projectedClipSkinnedUnits_ = 0.0;
    double projectedSharedRigidBatches_ = 0.0;
    double projectedGpuClipSkinBatches_ = 0.0;
    double projectedGpuClipPaletteBatches_ = 0.0;
    double projectedCpuRewriteBatches_ = 0.0;
    double projectedIndexedBatchesQueued_ = 0.0;
    EngineRenderBuildBreakdown renderBreakdown_{};
    EngineFixedPerfBreakdown fixedBreakdown_{};
    int fixedTicks_ = 0;
    int fixedTicksDropped_ = 0;
};

} // namespace game::runtime::perf_accum
