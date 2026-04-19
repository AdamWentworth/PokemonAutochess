#include <cmath>
#include <sstream>
#include <string>

#include "engine/core/EngineServices.h"
#include "game/runtime/loop/RuntimeGameRunnerFrameDiagnostics.h"

bool test_runtime_game_runner_frame_diagnostics_contract(std::string& outFail) {
    using game::runtime::runner_frame_diagnostics::Inputs;
    using game::runtime::runner_frame_diagnostics::makeInitialState;
    using game::runtime::runner_frame_diagnostics::observeAndEmit;

    EngineServices services;
    services.terminalLogMode = EngineTerminalLogMode::Performance;
    auto state = makeInitialState(services);
    std::ostringstream out;

    auto makeInputs = [](
                          double frameDt,
                          double frameCpuMs,
                          double beginFrameMs,
                          double renderBuildMs,
                          double submitRawMs,
                          double presentWaitMs,
                          std::uint32_t drawCalls,
                          std::uint32_t visibleAnimatedUnits,
                          int fixedTicks,
                          int fixedTicksDropped) {
        Inputs inputs;
        inputs.frameDt = frameDt;
        inputs.frameCpuMs = frameCpuMs;
        inputs.beginFrameMs = beginFrameMs;
        inputs.renderBuildMs = renderBuildMs;
        inputs.submitRawMs = submitRawMs;
        inputs.rendererHandlesPresentation = false;
        inputs.fixedPhase.fixedMs = frameCpuMs * 0.25;
        inputs.fixedPhase.fixedTickWorkMs = frameCpuMs * 0.20;
        inputs.fixedPhase.fixedTicks = fixedTicks;
        inputs.fixedPhase.fixedTicksDropped = fixedTicksDropped;
        inputs.fixedPhase.fixedBreakdown.combatMs = 1.0f;
        inputs.serviceSnapshot.visibleAnimatedUnits = visibleAnimatedUnits;
        inputs.serviceSnapshot.particleCount = 30u;
        inputs.serviceSnapshot.projectedUnitsMs = 2.0f;
        inputs.serviceSnapshot.rawRenderBreakdown.worldComposeMs = 1.0f;
        inputs.backendPerf.presentWaitMs = presentWaitMs;
        inputs.backendPerf.gpuFrameMs = 7.0;
        inputs.backendPerf.gpuFrameValid = true;
        inputs.backendPerf.drawCalls = drawCalls;
        inputs.backendPerf.triangles = 2000u + drawCalls;
        inputs.backendPerf.indexedCachedDraws = 6u;
        inputs.backendPerf.indexedMaterialSwitches = 4u;
        inputs.backendPerf.fastSceneInstances = 10u;
        inputs.backendPerf.fastSceneDrawClasses = 2u;
        inputs.backendPerf.fastSceneVisibleSkeletons = 1u;
        inputs.backendPerf.fastScenePaletteUploadBytes = 1024u;
        inputs.backendPerf.fastSceneMaterialTableBinds = 3u;
        inputs.backendPerf.fastSceneIndirectCommands = 4u;
        return inputs;
    };

    observeAndEmit(
        state,
        services,
        makeInputs(0.6, 16.0, 1.0, 5.0, 3.0, 1.0, 100u, 8u, 2, 1),
        out);
    if (!out.str().empty()) {
        outFail = "Frame diagnostics should not emit before the accumulator window is ready.";
        return false;
    }

    observeAndEmit(
        state,
        services,
        makeInputs(0.5, 20.0, 2.0, 7.0, 5.0, 3.0, 300u, 12u, 4, 3),
        out);

    const std::string perfOutput = out.str();
    if (perfOutput.find("[Perf] FPS=") == std::string::npos ||
        perfOutput.find("[PerfJSON] {") == std::string::npos) {
        outFail = "Frame diagnostics should emit both perf text and perf JSON when the accumulator window closes.";
        return false;
    }

    if (std::fabs(services.framePerf.renderBuildMs - 6.0f) > 0.001f ||
        std::fabs(services.framePerf.renderSubmitMs - 2.0f) > 0.001f ||
        std::fabs(services.framePerf.presentWaitMs - 2.0f) > 0.001f ||
        services.framePerf.drawCalls != 200u ||
        services.framePerf.visibleAnimatedUnits != 10u ||
        services.framePerf.fixedTicks != 3 ||
        services.framePerf.fixedTicksDropped != 2) {
        outFail = "Frame diagnostics should update EngineServices frame perf using the aggregated observation window.";
        return false;
    }

    out.str("");
    out.clear();
    auto hitchInputs = makeInputs(0.1, 28.0, 1.0, 2.0, 0.5, 0.4, 48u, 3u, 1, 0);
    hitchInputs.fixedPhase.fixedMs = 25.2;
    hitchInputs.fixedPhase.fixedBreakdown.combatMs = 25.2f;
    hitchInputs.serviceSnapshot.rawRenderBreakdown.worldVfxMs = 0.5f;
    observeAndEmit(state, services, hitchInputs, out);

    const std::string hitchOutput = out.str();
    if (hitchOutput.find("[PerfHitch] reason=frame+fixed+combat") == std::string::npos ||
        hitchOutput.find("[PerfHitchJSON] {") == std::string::npos) {
        outFail = "Frame diagnostics should emit single-frame hitch logs in Performance mode for visible stalls.";
        return false;
    }

    services.terminalLogMode = EngineTerminalLogMode::GrowlVfx;
    services.frameGrowlDebug.snapshotAvailable = true;
    services.frameGrowlDebug.activeRingCount = 2u;
    services.frameGrowlDebug.configuredPassCount = 4u;
    services.frameGrowlDebug.enabledPassCount = 2u;
    services.frameGrowlDebug.meshPassCount = 1u;
    services.frameGrowlDebug.activePasses.push_back({
        .id = "growl_eid_1076",
        .eid = 1076,
        .mode = "ring_mesh",
        .meshPath = "assets/meshes/growl_1076_mesh.glb",
        .texturePath = "assets/textures/moves/growl/Texture3924.png",
        .submittedBatchCount = 1u,
        .submittedVertexCount = 24u,
        .submittedIndexCount = 36u,
    });

    out.str("");
    out.clear();
    observeAndEmit(
        state,
        services,
        makeInputs(0.1, 16.0, 1.0, 5.0, 3.0, 1.0, 120u, 8u, 1, 0),
        out);

    const std::string growlOutput = out.str();
    if (growlOutput.find("[Growl] rings=2") == std::string::npos ||
        growlOutput.find("[GrowlJSON] {") == std::string::npos) {
        outFail = "Frame diagnostics should emit Growl debug logs when Growl VFX mode becomes active with live rings.";
        return false;
    }

    services.terminalLogMode = EngineTerminalLogMode::ScratchVfx;
    services.frameScratchDebug.snapshotAvailable = true;
    services.frameScratchDebug.activeGlowCount = 1u;
    services.frameScratchDebug.snapshotRingCount = 1u;
    services.frameScratchDebug.configuredPassCount = 39u;
    services.frameScratchDebug.enabledPassCount = 14u;
    services.frameScratchDebug.submittedBatchCount = 14u;
    services.frameScratchDebug.submittedAdditiveBatchCount = 13u;
    services.frameScratchDebug.submittedInstancedBatchCount = 13u;
    services.frameScratchDebug.submittedDynamicBatchCount = 1u;

    out.str("");
    out.clear();
    observeAndEmit(
        state,
        services,
        makeInputs(0.1, 16.0, 1.0, 5.0, 3.0, 1.0, 120u, 8u, 1, 0),
        out);

    const std::string scratchOutput = out.str();
    if (scratchOutput.find("[Scratch] Debug mode active") == std::string::npos ||
        scratchOutput.find("[ScratchPerf] reason=watch+start+rings+batches+spike glows=1") == std::string::npos ||
        scratchOutput.find("[ScratchPerfJSON] {") == std::string::npos) {
        outFail = "Frame diagnostics should emit Scratch perf logs when Scratch VFX mode is active with live glows.";
        return false;
    }

    out.str("");
    out.clear();
    auto quietScratchInputs = makeInputs(0.1, 16.0, 1.0, 1.0, 3.0, 1.0, 120u, 8u, 1, 0);
    quietScratchInputs.fixedPhase.fixedBreakdown.combatMs = 0.0f;
    quietScratchInputs.fixedPhase.fixedBreakdown.worldMs = 0.0f;
    observeAndEmit(
        state,
        services,
        quietScratchInputs,
        out);

    if (!out.str().empty()) {
        outFail = "Scratch diagnostics should stay quiet on unchanged non-spike frames.";
        return false;
    }

    services.terminalLogMode = EngineTerminalLogMode::TailFireDebug;
    out.str("");
    out.clear();
    observeAndEmit(
        state,
        services,
        makeInputs(0.1, 16.0, 1.0, 5.0, 3.0, 1.0, 120u, 8u, 1, 0),
        out);

    const std::string tailFireOutput = out.str();
    if (tailFireOutput.find("[TailFire] Debug mode active") == std::string::npos) {
        outFail = "Frame diagnostics should announce when Tail Fire Debug mode becomes active.";
        return false;
    }

    services.terminalLogMode = EngineTerminalLogMode::CombatDecision;
    out.str("");
    out.clear();
    observeAndEmit(
        state,
        services,
        makeInputs(0.1, 16.0, 1.0, 5.0, 3.0, 1.0, 120u, 8u, 1, 0),
        out);

    const std::string combatDecisionOutput = out.str();
    if (combatDecisionOutput.find("[CombatDecision] Debug mode active") == std::string::npos) {
        outFail = "Frame diagnostics should announce when Combat Decision mode becomes active.";
        return false;
    }
    if (combatDecisionOutput.find("first-use move traces") == std::string::npos) {
        outFail = "Combat Decision mode announcement should mention first-use move traces.";
        return false;
    }

    services.terminalLogMode = EngineTerminalLogMode::AnimationDecision;
    out.str("");
    out.clear();
    observeAndEmit(
        state,
        services,
        makeInputs(0.1, 16.0, 1.0, 5.0, 3.0, 1.0, 120u, 8u, 1, 0),
        out);

    const std::string animationDecisionOutput = out.str();
    if (animationDecisionOutput.find("[AnimTrace] Animation Decision mode active") ==
        std::string::npos) {
        outFail = "Frame diagnostics should announce when Animation Decision mode becomes active.";
        return false;
    }
    if (animationDecisionOutput.find("movement, locomotion, and attack animation selection") ==
        std::string::npos) {
        outFail = "Animation Decision mode announcement should mention animation selection traces.";
        return false;
    }

    return true;
}
