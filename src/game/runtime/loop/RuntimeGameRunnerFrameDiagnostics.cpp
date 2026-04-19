#include "game/runtime/loop/RuntimeGameRunnerFrameDiagnostics.h"

#include "engine/utils/LogSink.h"
#include "game/runtime/loop/RuntimePerfLogging.h"

#include <algorithm>
#include <ostream>
#include <string>

namespace game::runtime::runner_frame_diagnostics {

namespace {

EngineFramePerfStats makeInstantPerf(
    const game::runtime::perf_accum::FrameSample& sample) {
    game::runtime::perf_accum::RollingAccumulator accumulator;
    accumulator.addFrame(sample);
    return accumulator.makeSummaryAndReset().framePerf;
}

bool isScratchSpikeFrame(const EngineFramePerfStats& perf) {
    return perf.renderBuildMs >= 2.5f ||
           perf.renderBreakdown.worldVfxMs >= 1.0f ||
           perf.fixedBreakdown.combatMs >= 0.5f ||
           perf.fixedBreakdown.worldMs >= 0.5f;
}

bool isPerfHitchFrame(const EngineFramePerfStats& perf, const State& state) {
    const bool absoluteFrame = perf.frameMs >= 25.0f;
    const bool frameJump =
        state.previousInstantFrameMs > 0.0f &&
        perf.frameMs >= std::max(8.0f, state.previousInstantFrameMs * 4.0f);
    const bool fixedSpike = perf.fixedMs >= 8.0f;
    const bool buildSpike = perf.renderBuildMs >= 8.0f;
    const bool submitSpike = perf.renderSubmitMs >= 4.0f;
    const bool presentSpike = perf.presentWaitMs >= 12.0f;
    const bool gpuSpike = perf.gpuFrameValid && perf.gpuFrameMs >= 12.0f;
    const bool combatSpike = perf.fixedBreakdown.combatMs >= 4.0f;
    const bool worldSpike = perf.fixedBreakdown.worldMs >= 4.0f;
    const bool vfxSpike = perf.renderBreakdown.worldVfxMs >= 4.0f;
    const bool droppedTicks = perf.fixedTicksDropped >= 2;
    return absoluteFrame || frameJump || fixedSpike || buildSpike || submitSpike ||
           presentSpike || gpuSpike || combatSpike || worldSpike || vfxSpike || droppedTicks;
}

std::string perfHitchReason(const EngineFramePerfStats& perf, const State& state) {
    std::string reason;
    const auto appendReason = [&](const char* token) {
        if (!reason.empty()) reason += "+";
        reason += token;
    };

    if (perf.frameMs >= 25.0f) appendReason("frame");
    if (state.previousInstantFrameMs > 0.0f &&
        perf.frameMs >= std::max(8.0f, state.previousInstantFrameMs * 4.0f)) {
        appendReason("jump");
    }
    if (perf.fixedMs >= 8.0f) appendReason("fixed");
    if (perf.renderBuildMs >= 8.0f) appendReason("build");
    if (perf.renderSubmitMs >= 4.0f) appendReason("submit");
    if (perf.presentWaitMs >= 12.0f) appendReason("present");
    if (perf.gpuFrameValid && perf.gpuFrameMs >= 12.0f) appendReason("gpu");
    if (perf.fixedBreakdown.combatMs >= 4.0f) appendReason("combat");
    if (perf.fixedBreakdown.worldMs >= 4.0f) appendReason("world");
    if (perf.renderBreakdown.worldVfxMs >= 4.0f) appendReason("vfx");
    if (perf.fixedTicksDropped >= 2) appendReason("drop");

    if (reason.empty()) reason = "watch";
    return reason;
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
    const EngineFramePerfStats instantPerf = makeInstantPerf(currentFrameSample);
    state.perfAccumulator.addFrame(currentFrameSample);
    if (state.perfAccumulator.readyToEmit()) {
        const auto perfSummary = state.perfAccumulator.makeSummaryAndReset();
        services.framePerf = perfSummary.framePerf;
        if (services.terminalLogMode == EngineTerminalLogMode::Performance) {
            log.info(game::runtime::perf_logging::formatPerfLine(services.framePerf));
            log.info(game::runtime::perf_logging::formatPerfJson(services.framePerf));
        }
    }
    if (services.terminalLogMode == EngineTerminalLogMode::Performance &&
        isPerfHitchFrame(instantPerf, state)) {
        const std::string reason = perfHitchReason(instantPerf, state);
        log.info(game::runtime::perf_logging::formatPerfHitchLine(instantPerf, reason));
        log.info(game::runtime::perf_logging::formatPerfHitchJson(instantPerf, reason));
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
    if (services.terminalLogMode == EngineTerminalLogMode::CombatDecision &&
        state.previousTerminalLogMode != EngineTerminalLogMode::CombatDecision) {
        log.info(
            "[CombatDecision] Debug mode active; decision spike traces and first-use move traces will emit during combat.");
    }
    if (services.terminalLogMode == EngineTerminalLogMode::AnimationDecision &&
        state.previousTerminalLogMode != EngineTerminalLogMode::AnimationDecision) {
        log.info(
            "[AnimTrace] Animation Decision mode active; movement, locomotion, and attack animation selection traces will emit during combat.");
    }

    state.previousGrowlRingCount = services.frameGrowlDebug.activeRingCount;
    state.previousScratchGlowCount = services.frameScratchDebug.activeGlowCount;
    state.previousScratchRingCount = services.frameScratchDebug.snapshotRingCount;
    state.previousScratchBatchCount = services.frameScratchDebug.submittedBatchCount;
    state.previousInstantFrameMs = instantPerf.frameMs;
    state.previousTerminalLogMode = services.terminalLogMode;
}

} // namespace game::runtime::runner_frame_diagnostics
