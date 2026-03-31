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

    return true;
}
