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
    EngineFixedPerfBreakdown fixedBreakdown{};
    int fixedTicks = 0;
    int fixedTicksDropped = 0;
};

ServiceSnapshot captureServiceSnapshot(const EngineServices& services);

game::runtime::perf_accum::FrameSample makePerfSample(const SampleInputs& inputs,
                                                      const ServiceSnapshot& snapshot);

} // namespace game::runtime::frame_observation

