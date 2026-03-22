#include <cmath>
#include <string>

#include "game/runtime/loop/RuntimeFramePerfCapture.h"

bool test_runtime_frame_perf_capture_contract(std::string& outFail) {
    using game::runtime::frame_perf_capture::BackendFrameInputs;
    using game::runtime::frame_perf_capture::computeSubmitMs;
    using game::runtime::frame_perf_capture::computeTotalPresentWaitMs;
    using game::runtime::frame_perf_capture::finalizeRenderBreakdown;
    using game::runtime::frame_perf_capture::resolveBackendFrameOutputs;

    {
        BackendFrameInputs inputs;
        inputs.rendererHandlesPresentation = true;
        inputs.hasBackendTimings = true;
        inputs.backendTimings.presentWaitMs = 2.5f;
        inputs.backendTimings.gpuFrameMs = 5.0f;
        inputs.backendTimings.gpuFrameValid = true;
        inputs.hasBackendStats = true;
        inputs.backendStats.drawCalls = 42;
        inputs.backendStats.triangles = 1337;
        inputs.backendStats.fastSceneInstances = 15;
        inputs.backendStats.fastSceneDrawClasses = 3;
        inputs.backendStats.fastSceneVisibleSkeletons = 2;
        inputs.backendStats.fastScenePaletteUploadBytes = 4096;
        inputs.backendStats.fastSceneMaterialTableBinds = 4;
        inputs.backendStats.fastSceneIndirectCommands = 6;
        const auto out = resolveBackendFrameOutputs(inputs);
        if (std::fabs(out.presentWaitMs - 2.5) > 0.0001 ||
            std::fabs(out.gpuFrameMs - 5.0) > 0.0001 ||
            !out.gpuFrameValid ||
            out.drawCalls != 42 ||
            out.triangles != 1337 ||
            out.fastSceneInstances != 15 ||
            out.fastSceneDrawClasses != 3 ||
            out.fastSceneVisibleSkeletons != 2 ||
            out.fastScenePaletteUploadBytes != 4096u ||
            out.fastSceneMaterialTableBinds != 4 ||
            out.fastSceneIndirectCommands != 6) {
            outFail = "resolveBackendFrameOutputs should honor backend timings and stats.";
            return false;
        }
    }

    {
        BackendFrameInputs inputs;
        inputs.rendererHandlesPresentation = false;
        inputs.measuredPresentWaitMs = 7.5;
        const auto out = resolveBackendFrameOutputs(inputs);
        if (std::fabs(out.presentWaitMs - 7.5) > 0.0001 || out.gpuFrameValid) {
            outFail = "resolveBackendFrameOutputs should use measured swap wait when the backend does not present.";
            return false;
        }
    }

    if (std::fabs(computeSubmitMs(8.0, 2.5) - 5.5) > 0.0001 ||
        std::fabs(computeSubmitMs(1.0, 2.5) - 0.0) > 0.0001) {
        outFail = "computeSubmitMs should subtract present wait without going negative.";
        return false;
    }

    if (std::fabs(computeTotalPresentWaitMs(true, 1.0, 2.0) - 3.0) > 0.0001 ||
        std::fabs(computeTotalPresentWaitMs(false, 1.0, 2.0) - 2.0) > 0.0001) {
        outFail = "computeTotalPresentWaitMs should add beginFrame time only for backend-presented paths.";
        return false;
    }

    {
        EngineRenderBuildBreakdown raw{};
        raw.worldComposeMs = 1.0f;
        raw.overlayPrepMs = 1.5f;
        raw.worldBackgroundMs = 0.5f;
        raw.worldTriangles3dMs = 2.0f;
        raw.worldIndexedMs = 0.5f;
        raw.worldDebugMs = 0.25f;
        raw.spriteMs = 0.25f;
        raw.uiMs = 0.5f;
        const auto out = finalizeRenderBreakdown(10.0, 1.0f, raw);
        if (std::fabs(out.otherMs - 2.5f) > 0.001f) {
            outFail = "finalizeRenderBreakdown should attribute remaining render-build time to otherMs.";
            return false;
        }
    }

    {
        EngineRenderBuildBreakdown raw{};
        raw.worldComposeMs = 10.0f;
        const auto out = finalizeRenderBreakdown(1.0, 0.0f, raw);
        if (std::fabs(out.otherMs) > 0.001f) {
            outFail = "finalizeRenderBreakdown should clamp negative otherMs to zero.";
            return false;
        }
    }

    return true;
}

