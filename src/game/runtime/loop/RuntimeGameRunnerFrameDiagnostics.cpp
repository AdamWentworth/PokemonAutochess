#include "game/runtime/loop/RuntimeGameRunnerFrameDiagnostics.h"

#include "engine/utils/LogSink.h"
#include "game/runtime/loop/RuntimePerfLogging.h"

#include <algorithm>
#include <ostream>
#include <string>

namespace game::runtime::runner_frame_diagnostics {

namespace {

bool isScratchSpikeFrame(const EngineFramePerfStats& perf) {
    return perf.renderBuildMs >= 2.5f ||
           perf.renderBreakdown.worldVfxMs >= 1.0f ||
           perf.fixedBreakdown.combatMs >= 0.5f ||
           perf.fixedBreakdown.worldMs >= 0.5f;
}

std::string scratchEmitReason(const State& state,
                              const EngineScratchDebugStats& scratchDebug,
                              const EngineFramePerfStats& perf,
                              bool modeJustSwitchedToScratch) {
    if (scratchDebug.activeGlowCount == 0u) return {};

    const bool firstActive =
        state.previousScratchGlowCount == 0u && scratchDebug.activeGlowCount > 0u;
    const bool ringChanged =
        scratchDebug.snapshotRingCount != state.previousScratchRingCount;
    const bool batchChanged =
        scratchDebug.submittedBatchCount != state.previousScratchBatchCount;
    const bool spike = isScratchSpikeFrame(perf);
    const bool spikeStarted = spike && !state.previousScratchSpike;

    std::string reason;
    const auto appendReason = [&](const char* token) {
        if (!reason.empty()) reason += "+";
        reason += token;
    };

    if (modeJustSwitchedToScratch) appendReason("watch");
    if (firstActive) appendReason("start");
    if (ringChanged) appendReason("rings");
    if (batchChanged) appendReason("batches");
    if (spikeStarted) appendReason("spike");

    return reason;
}

} // namespace

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

    const auto currentFrameSample =
        game::runtime::frame_observation::makePerfSample(
            sampleInputs,
            inputs.serviceSnapshot);
    state.perfAccumulator.addFrame(currentFrameSample);
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
    if (services.terminalLogMode == EngineTerminalLogMode::ScratchVfx) {
        const bool modeJustSwitchedToScratch =
            state.previousTerminalLogMode != EngineTerminalLogMode::ScratchVfx;
        bool currentScratchSpike = false;
        if (modeJustSwitchedToScratch) {
            log.info(
                "[Scratch] Debug mode active; Scratch perf logs will emit on start, state changes, and spike frames.");
        }
        if (services.frameScratchDebug.activeGlowCount > 0u) {
            game::runtime::perf_accum::RollingAccumulator instantAccumulator;
            instantAccumulator.addFrame(currentFrameSample);
            const EngineFramePerfStats instantPerf =
                instantAccumulator.makeSummaryAndReset().framePerf;
            currentScratchSpike = isScratchSpikeFrame(instantPerf);
            const std::string scratchReason =
                scratchEmitReason(
                    state,
                    services.frameScratchDebug,
                    instantPerf,
                    modeJustSwitchedToScratch);
            if (!scratchReason.empty()) {
                log.info(game::runtime::perf_logging::formatScratchDebugLine(
                    services.frameScratchDebug,
                    instantPerf,
                    scratchReason));
                log.info(game::runtime::perf_logging::formatScratchDebugJson(
                    services.frameScratchDebug,
                    instantPerf,
                    scratchReason));
            }
        }
        state.previousScratchSpike = currentScratchSpike;
    } else {
        state.previousScratchSpike = false;
    }
    if (services.terminalLogMode == EngineTerminalLogMode::TailFireDebug &&
        state.previousTerminalLogMode != EngineTerminalLogMode::TailFireDebug) {
        log.info(
            "[TailFire] Debug mode active; Tail Fire anchor and billboard logs will emit on active paths.");
    }

    state.previousGrowlRingCount = services.frameGrowlDebug.activeRingCount;
    state.previousScratchGlowCount = services.frameScratchDebug.activeGlowCount;
    state.previousScratchRingCount = services.frameScratchDebug.snapshotRingCount;
    state.previousScratchBatchCount = services.frameScratchDebug.submittedBatchCount;
    state.previousTerminalLogMode = services.terminalLogMode;
}

} // namespace game::runtime::runner_frame_diagnostics
