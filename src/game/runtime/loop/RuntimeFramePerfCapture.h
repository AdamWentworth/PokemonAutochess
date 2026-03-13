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
