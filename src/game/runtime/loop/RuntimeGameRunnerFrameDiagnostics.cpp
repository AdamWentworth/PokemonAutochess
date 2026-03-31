#include "game/runtime/loop/RuntimeGameRunnerFrameDiagnostics.h"

#include "engine/utils/LogSink.h"
#include "game/runtime/loop/RuntimePerfLogging.h"

#include <algorithm>
#include <ostream>

namespace game::runtime::runner_frame_diagnostics {

State makeInitialState(const EngineServices& services) {
    State state;
    state.previousTerminalLogMode = services.terminalLogMode;
    return state;
}

void observeAndEmit(State& state,
                    EngineServices& services,
                    const Inputs& inputs,
                    std::ostream& out) {
    engine::log::Sink log("RunDiag", &out, nullptr);
    const double submitMs =
        game::runtime::frame_perf_capture::computeSubmitMs(
            inputs.submitRawMs,
            inputs.backendPerf.presentWaitMs);
    const double totalPresentWaitMs =
        game::runtime::frame_perf_capture::computeTotalPresentWaitMs(
            inputs.rendererHandlesPresentation,
            inputs.beginFrameMs,
            inputs.backendPerf.presentWaitMs);
    const double legacyRenderMs = inputs.beginFrameMs + inputs.renderBuildMs;
    const double legacySwapMs = std::max(0.0, inputs.submitRawMs);

    game::runtime::frame_observation::SampleInputs sampleInputs;
    sampleInputs.frameDt = inputs.frameDt;
    sampleInputs.frameCpuMs = inputs.frameCpuMs;
    sampleInputs.fixedMs = inputs.fixedPhase.fixedMs;
    sampleInputs.fixedTickWorkMs = inputs.fixedPhase.fixedTickWorkMs;
    sampleInputs.renderBuildMs = inputs.renderBuildMs;
    sampleInputs.renderSubmitMs = submitMs;
    sampleInputs.presentWaitMs = totalPresentWaitMs;
    sampleInputs.legacyRenderMs = legacyRenderMs;
    sampleInputs.legacySwapMs = legacySwapMs;
    sampleInputs.gpuFrameMs = inputs.backendPerf.gpuFrameMs;
    sampleInputs.gpuFrameValid = inputs.backendPerf.gpuFrameValid;
    sampleInputs.drawCalls = inputs.backendPerf.drawCalls;
    sampleInputs.triangles = inputs.backendPerf.triangles;
    sampleInputs.indexedOpaqueDraws = inputs.backendPerf.indexedOpaqueDraws;
    sampleInputs.indexedBlendDraws = inputs.backendPerf.indexedBlendDraws;
    sampleInputs.indexedCachedDraws = inputs.backendPerf.indexedCachedDraws;
    sampleInputs.indexedDynamicDraws = inputs.backendPerf.indexedDynamicDraws;
    sampleInputs.indexedInstancedDraws = inputs.backendPerf.indexedInstancedDraws;
    sampleInputs.indexedOutlineBatches = inputs.backendPerf.indexedOutlineBatches;
    sampleInputs.indexedGeometrySwitches = inputs.backendPerf.indexedGeometrySwitches;
    sampleInputs.indexedMaterialSwitches = inputs.backendPerf.indexedMaterialSwitches;
    sampleInputs.indexedTextureSwitches = inputs.backendPerf.indexedTextureSwitches;
    sampleInputs.indexedGlTextureBindCalls = inputs.backendPerf.indexedGlTextureBindCalls;
    sampleInputs.indexedD3d12PsoSets = inputs.backendPerf.indexedD3d12PsoSets;
    sampleInputs.indexedD3d12DescriptorTableSets =
        inputs.backendPerf.indexedD3d12DescriptorTableSets;
    sampleInputs.fastSceneInstances = inputs.backendPerf.fastSceneInstances;
    sampleInputs.fastSceneDrawClasses = inputs.backendPerf.fastSceneDrawClasses;
    sampleInputs.fastSceneVisibleSkeletons = inputs.backendPerf.fastSceneVisibleSkeletons;
    sampleInputs.fastScenePaletteUploadBytes = inputs.backendPerf.fastScenePaletteUploadBytes;
    sampleInputs.fastSceneMaterialTableBinds = inputs.backendPerf.fastSceneMaterialTableBinds;
    sampleInputs.fastSceneIndirectCommands = inputs.backendPerf.fastSceneIndirectCommands;
    sampleInputs.fixedBreakdown = inputs.fixedPhase.fixedBreakdown;
    sampleInputs.fixedTicks = inputs.fixedPhase.fixedTicks;
    sampleInputs.fixedTicksDropped = inputs.fixedPhase.fixedTicksDropped;

    state.perfAccumulator.addFrame(
        game::runtime::frame_observation::makePerfSample(
            sampleInputs,
            inputs.serviceSnapshot));
    if (state.perfAccumulator.readyToEmit()) {
        const auto perfSummary = state.perfAccumulator.makeSummaryAndReset();
        services.framePerf = perfSummary.framePerf;
        if (services.terminalLogMode == EngineTerminalLogMode::Performance) {
            log.info(game::runtime::perf_logging::formatPerfLine(services.framePerf));
            log.info(game::runtime::perf_logging::formatPerfJson(services.framePerf));
        }
    }

    if (services.terminalLogMode == EngineTerminalLogMode::GrowlVfx) {
        const std::uint32_t currentGrowlRingCount =
            services.frameGrowlDebug.activeRingCount;
        const bool modeJustSwitchedToGrowl =
            state.previousTerminalLogMode != EngineTerminalLogMode::GrowlVfx;
        const bool growlStartedThisFrame =
            currentGrowlRingCount > state.previousGrowlRingCount;
        if (currentGrowlRingCount > 0u &&
            (modeJustSwitchedToGrowl || growlStartedThisFrame)) {
            log.info(game::runtime::perf_logging::formatGrowlDebugLine(
                services.frameGrowlDebug));
            log.info(game::runtime::perf_logging::formatGrowlDebugJson(
                services.frameGrowlDebug));
        }
    }
    if (services.terminalLogMode == EngineTerminalLogMode::TailFireDebug &&
        state.previousTerminalLogMode != EngineTerminalLogMode::TailFireDebug) {
        log.info(
            "[TailFire] Debug mode active; Tail Fire anchor and billboard logs will emit on active paths.");
    }

    state.previousGrowlRingCount = services.frameGrowlDebug.activeRingCount;
    state.previousTerminalLogMode = services.terminalLogMode;
}

} // namespace game::runtime::runner_frame_diagnostics
