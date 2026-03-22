#pragma once

#include <cstdint>

#include "engine/core/EngineServices.h"
#include "engine/render/IRenderBackend.h"

namespace game::runtime::frame_perf_capture {

struct BackendFrameInputs {
    bool rendererHandlesPresentation = false;
    bool hasBackendTimings = false;
    IRenderBackend::BackendFrameTimings backendTimings{};
    bool hasBackendStats = false;
    IRenderBackend::BackendFrameStats backendStats{};
    double measuredPresentWaitMs = 0.0;
};

struct BackendFrameOutputs {
    double presentWaitMs = 0.0;
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
};

BackendFrameOutputs resolveBackendFrameOutputs(const BackendFrameInputs& inputs);

double computeSubmitMs(double submitRawMs, double presentWaitMs);

double computeTotalPresentWaitMs(bool rendererHandlesPresentation,
                                 double beginFrameMs,
                                 double presentWaitMs);

EngineRenderBuildBreakdown finalizeRenderBreakdown(double renderBuildMs,
                                                   float projectedUnitsMs,
                                                   const EngineRenderBuildBreakdown& rawBreakdown);

} // namespace game::runtime::frame_perf_capture
