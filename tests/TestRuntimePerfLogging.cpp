#include <string>

#include "game/runtime/loop/RuntimePerfLogging.h"

bool test_runtime_perf_logging_contract(std::string& outFail) {
    if (std::string(game::runtime::perf_logging::terminalLogModeName(
            EngineTerminalLogMode::Performance)) != "Performance" ||
        std::string(game::runtime::perf_logging::terminalLogModeName(
            EngineTerminalLogMode::GrowlVfx)) != "Growl VFX" ||
        std::string(game::runtime::perf_logging::terminalLogModeName(
            EngineTerminalLogMode::ScratchVfx)) != "Scratch VFX" ||
        std::string(game::runtime::perf_logging::terminalLogModeName(
            EngineTerminalLogMode::CombatDecision)) != "Combat Decision" ||
        std::string(game::runtime::perf_logging::terminalLogModeName(
            EngineTerminalLogMode::TailFireDebug)) != "Tail Fire Debug") {
        outFail = "terminalLogModeName should expose stable terminal mode labels.";
        return false;
    }

    if (game::runtime::perf_logging::nextTerminalLogMode(
            EngineTerminalLogMode::Performance) != EngineTerminalLogMode::GrowlVfx ||
        game::runtime::perf_logging::nextTerminalLogMode(
            EngineTerminalLogMode::GrowlVfx) != EngineTerminalLogMode::ScratchVfx ||
        game::runtime::perf_logging::nextTerminalLogMode(
            EngineTerminalLogMode::ScratchVfx) != EngineTerminalLogMode::CombatDecision ||
        game::runtime::perf_logging::nextTerminalLogMode(
            EngineTerminalLogMode::CombatDecision) != EngineTerminalLogMode::TailFireDebug ||
        game::runtime::perf_logging::nextTerminalLogMode(
            EngineTerminalLogMode::TailFireDebug) != EngineTerminalLogMode::Performance) {
        outFail = "nextTerminalLogMode should cycle Performance -> Growl VFX -> Scratch VFX -> Combat Decision -> Tail Fire Debug -> Performance.";
        return false;
    }

    EngineFramePerfStats perf;
    perf.fps = 60.0f;
    perf.frameMs = 16.7f;
    perf.fixedMs = 4.2f;
    perf.fixedTickMs = 1.0f;
    perf.renderBuildMs = 5.3f;
    perf.renderSubmitMs = 1.2f;
    perf.presentWaitMs = 2.4f;
    perf.gpuFrameValid = false;
    perf.drawCalls = 123;
    perf.triangles = 4567;
    perf.visibleAnimatedUnits = 9;
    perf.particleCount = 25;
    perf.projectedUnitsMs = 1.5f;
    perf.projectedPoseEvalMs = 0.4f;
    perf.projectedModelMs = 0.8f;
    perf.projectedModelPrepMs = 0.3f;
    perf.projectedModelGeometryMs = 0.2f;
    perf.projectedOverlayMs = 0.6f;
    perf.projectedClipSkinnedUnits = 3;
    perf.fastSceneInstances = 24u;
    perf.fastSceneDrawClasses = 6u;
    perf.fastSceneVisibleSkeletons = 2u;
    perf.fastScenePaletteUploadBytes = 4096u;
    perf.fastSceneMaterialTableBinds = 5u;
    perf.fastSceneIndirectCommands = 7u;
    perf.renderMs = 6.5f;
    perf.swapMs = 2.1f;
    perf.fixedTicks = 3;
    perf.fixedTicksDropped = 1;
    perf.fixedBreakdown.backendHydrateMs = 0.2f;
    perf.fixedBreakdown.combatMs = 0.8f;
    perf.fixedBreakdown.worldMs = 0.6f;
    perf.fixedBreakdown.cameraMs = 0.03f;

    const std::string fixed = game::runtime::perf_logging::formatTopFixedSystems(perf.fixedBreakdown);
    if (fixed.find("combat:0.8ms") == std::string::npos ||
        fixed.find("world:0.6ms") == std::string::npos ||
        fixed.find("backend_hydrate:0.2ms") == std::string::npos ||
        fixed.find("camera") != std::string::npos) {
        outFail = "formatTopFixedSystems should emit the top three significant fixed-system costs.";
        return false;
    }

    const std::string line = game::runtime::perf_logging::formatPerfLine(perf);
    if (line.find("[Perf] FPS=60.0") == std::string::npos ||
        line.find("frame=16.7ms") == std::string::npos ||
        line.find("gpu=-1.0ms") == std::string::npos ||
        line.find("draws=123") == std::string::npos ||
        line.find("fscene=i24/d6/s2/b5") == std::string::npos ||
        line.find("drop=1") == std::string::npos) {
        outFail = "formatPerfLine should emit the expected human-readable perf fields.";
        return false;
    }

    perf.gpuFrameValid = true;
    perf.gpuFrameMs = 8.9f;
    perf.indexedCachedDraws = 4u;
    perf.indexedMaterialSwitches = 3u;
    perf.indexedGlTextureBindCalls = 12u;
    perf.renderBreakdown.worldComposeMs = 1.1f;
    perf.renderBreakdown.worldSceneSubmitMs = 0.4f;
    perf.renderBreakdown.worldIndexedBatchSubmitMs = 0.5f;
    perf.fixedBreakdown.preUpdateMs = 0.2f;
    const std::string json = game::runtime::perf_logging::formatPerfJson(perf);
    if (json.find("[PerfJSON] {") != 0 ||
        json.find("\"gpu_frame_valid\":1") == std::string::npos ||
        json.find("\"gpu_frame_ms\":8.900") == std::string::npos ||
        json.find("\"backend_indexed_cached_draws\":4") == std::string::npos ||
        json.find("\"backend_indexed_material_switches\":3") == std::string::npos ||
        json.find("\"backend_gl_texture_bind_calls\":12") == std::string::npos ||
        json.find("\"backend_fast_scene_instances\":24") == std::string::npos ||
        json.find("\"backend_fast_scene_draw_classes\":6") == std::string::npos ||
        json.find("\"backend_fast_scene_palette_upload_bytes\":4096") == std::string::npos ||
        json.find("\"render_world_compose_ms\":1.100") == std::string::npos ||
        json.find("\"render_world_scene_submit_ms\":0.400") == std::string::npos ||
        json.find("\"render_world_indexed_batch_submit_ms\":0.500") == std::string::npos ||
        json.find("\"fixed_phase_pre_ms\":0.200") == std::string::npos ||
        json.find("\"fixed_ticks_dropped\":1") == std::string::npos) {
        outFail = "formatPerfJson should emit stable JSON-style perf fields.";
        return false;
    }

    EngineGrowlDebugStats growl;
    growl.snapshotAvailable = true;
    growl.activeRingCount = 3u;
    growl.configuredPassCount = 9u;
    growl.enabledPassCount = 2u;
    growl.meshPassCount = 1u;
    growl.linePassCount = 0u;
    growl.quarterRingPassCount = 0u;
    growl.quarterTextureBakePassCount = 1u;
    growl.activePasses.push_back({
        .id = "growl_eid_1255",
        .eid = 1255,
        .mode = "sparkle_mesh",
        .meshPath = "assets/meshes/growl_1255_mesh.glb",
        .texturePath = "assets/textures/moves/growl/Texture3924.png",
        .quarterTextureBake = true,
        .linePass = false,
        .scaleMul = 0.25f,
        .alphaMul = 1.0f,
        .forwardOffset = 0.0f,
        .submittedBatchCount = 1u,
        .submittedVertexCount = 40u,
        .submittedIndexCount = 60u,
        .submittedTextureWidth = 128,
        .submittedTextureHeight = 128,
        .submittedTranslateX = 1.25f,
        .submittedTranslateY = 2.50f,
        .submittedTranslateZ = 3.75f,
    });

    const std::string growlLine = game::runtime::perf_logging::formatGrowlDebugLine(growl);
    if (growlLine.find("[Growl] rings=3") != 0 ||
        growlLine.find("quarter_tex=1") == std::string::npos ||
        growlLine.find("1255:sparkle_mesh[b1/v40/i60]") == std::string::npos) {
        outFail = "formatGrowlDebugLine should emit the expected Growl summary fields.";
        return false;
    }

    const std::string growlJson = game::runtime::perf_logging::formatGrowlDebugJson(growl);
    if (growlJson.find("[GrowlJSON] {") != 0 ||
        growlJson.find("\"snapshot_available\":1") == std::string::npos ||
        growlJson.find("\"active_rings\":3") == std::string::npos ||
        growlJson.find("\"id\":\"growl_eid_1255\"") == std::string::npos ||
        growlJson.find("\"mesh\":\"assets/meshes/growl_1255_mesh.glb\"") == std::string::npos ||
        growlJson.find("\"quarter_texture_bake\":1") == std::string::npos ||
        growlJson.find("\"submitted_batches\":1") == std::string::npos ||
        growlJson.find("\"submitted_vertices\":40") == std::string::npos ||
        growlJson.find("\"submitted_indices\":60") == std::string::npos ||
        growlJson.find("\"submitted_texture_width\":128") == std::string::npos ||
        growlJson.find("\"submitted_translate_y\":2.500") == std::string::npos) {
        outFail = "formatGrowlDebugJson should emit stable JSON-style Growl debug fields.";
        return false;
    }

    EngineScratchDebugStats scratch;
    scratch.snapshotAvailable = true;
    scratch.activeGlowCount = 2u;
    scratch.snapshotRingCount = 2u;
    scratch.configuredPassCount = 39u;
    scratch.enabledPassCount = 14u;
    scratch.submittedBatchCount = 14u;
    scratch.submittedAlphaBatchCount = 1u;
    scratch.submittedAdditiveBatchCount = 13u;
    scratch.submittedInstancedBatchCount = 13u;
    scratch.submittedDynamicBatchCount = 1u;
    scratch.submittedInstanceCount = 18u;
    scratch.submittedVertexCount = 240u;
    scratch.submittedIndexCount = 360u;

    perf.frameMs = 3.8f;
    perf.renderBuildMs = 1.5f;
    perf.gpuFrameValid = true;
    perf.gpuFrameMs = 0.8f;
    perf.drawCalls = 36u;
    perf.indexedTextureSwitches = 26u;
    perf.indexedGlTextureBindCalls = 37u;
    perf.renderBreakdown.worldVfxMs = 0.49f;
    perf.fixedBreakdown.combatMs = 0.50f;
    perf.fixedBreakdown.worldMs = 0.41f;

    const std::string scratchLine =
        game::runtime::perf_logging::formatScratchDebugLine(scratch, perf, "start+spike");
    if (scratchLine.find("[ScratchPerf] reason=start+spike glows=2") != 0 ||
        scratchLine.find("batches=14") == std::string::npos ||
        scratchLine.find("add=13") == std::string::npos ||
        scratchLine.find("frame=3.80ms") == std::string::npos ||
        scratchLine.find("vfx=0.49ms") == std::string::npos ||
        scratchLine.find("combat=0.50ms") == std::string::npos) {
        outFail = "formatScratchDebugLine should emit the expected Scratch perf summary fields.";
        return false;
    }

    const std::string scratchJson =
        game::runtime::perf_logging::formatScratchDebugJson(scratch, perf, "start+spike");
    if (scratchJson.find("[ScratchPerfJSON] {") != 0 ||
        scratchJson.find("\"reason\":\"start+spike\"") == std::string::npos ||
        scratchJson.find("\"snapshot_available\":1") == std::string::npos ||
        scratchJson.find("\"active_glows\":2") == std::string::npos ||
        scratchJson.find("\"submitted_additive_batches\":13") == std::string::npos ||
        scratchJson.find("\"draw_calls\":36") == std::string::npos ||
        scratchJson.find("\"render_world_vfx_ms\":0.490") == std::string::npos ||
        scratchJson.find("\"fixed_world_ms\":0.410") == std::string::npos) {
        outFail = "formatScratchDebugJson should emit stable JSON-style Scratch perf fields.";
        return false;
    }

    perf.frameMs = 28.1f;
    perf.fixedMs = 25.2f;
    perf.renderBuildMs = 1.8f;
    perf.renderSubmitMs = 0.2f;
    perf.presentWaitMs = 0.7f;
    perf.gpuFrameValid = true;
    perf.gpuFrameMs = 0.3f;
    perf.fixedBreakdown.combatMs = 25.2f;
    perf.fixedBreakdown.updatePhaseMs = 25.4f;
    perf.fixedBreakdown.phaseTransitionMs = 1.7f;
    perf.fixedBreakdown.roundMs = 0.8f;
    perf.fixedBreakdown.stateManagerMs = 0.6f;
    perf.fixedBreakdown.stateUpdateMs = 0.4f;
    perf.fixedBreakdown.stateFlushMs = 0.2f;
    perf.fixedBreakdown.worldMs = 0.0f;
    perf.renderBreakdown.worldVfxMs = 0.5f;
    perf.projectedUnitsMs = 1.4f;
    perf.projectedModelMs = 0.8f;
    perf.projectedModelPrepMs = 0.3f;
    perf.projectedModelGeometryMs = 0.2f;
    perf.renderBreakdown.worldComposeMs = 0.6f;
    perf.renderBreakdown.worldIndexedMs = 0.7f;
    perf.renderBreakdown.worldSceneSubmitMs = 0.2f;
    perf.renderBreakdown.worldIndexedBatchSubmitMs = 0.5f;
    perf.renderBreakdown.overlayPrepMs = 0.4f;
    perf.renderBreakdown.otherMs = 1.1f;
    perf.drawCalls = 48u;
    perf.triangles = 17982u;
    perf.visibleAnimatedUnits = 3u;
    perf.particleCount = 0u;
    perf.fixedTicks = 1;
    perf.fixedTicksDropped = 0;

    const std::string hitchLine =
        game::runtime::perf_logging::formatPerfHitchLine(perf, "frame+combat");
    if (hitchLine.find("[PerfHitch] reason=frame+combat") != 0 ||
        hitchLine.find("frame=28.10ms") == std::string::npos ||
        hitchLine.find("combat=25.20ms") == std::string::npos ||
        hitchLine.find("proj=1.40ms") == std::string::npos ||
        hitchLine.find("indexed=0.70ms") == std::string::npos ||
        hitchLine.find("scene=0.20ms") == std::string::npos ||
        hitchLine.find("ibatch=0.50ms") == std::string::npos ||
        hitchLine.find("fsys=combat:25.2ms") == std::string::npos ||
        hitchLine.find("draws=48") == std::string::npos) {
        outFail = "formatPerfHitchLine should emit the expected single-frame hitch summary fields.";
        return false;
    }

    const std::string hitchJson =
        game::runtime::perf_logging::formatPerfHitchJson(perf, "frame+combat");
    if (hitchJson.find("[PerfHitchJSON] {") != 0 ||
        hitchJson.find("\"reason\":\"frame+combat\"") == std::string::npos ||
        hitchJson.find("\"frame_cpu_ms\":28.100") == std::string::npos ||
        hitchJson.find("\"fixed_combat_ms\":25.200") == std::string::npos ||
        hitchJson.find("\"fixed_phase_transition_ms\":1.700") == std::string::npos ||
        hitchJson.find("\"fixed_state_flush_ms\":0.200") == std::string::npos ||
        hitchJson.find("\"projected_model_prep_ms\":0.300") == std::string::npos ||
        hitchJson.find("\"render_world_indexed_ms\":0.700") == std::string::npos ||
        hitchJson.find("\"render_world_scene_submit_ms\":0.200") == std::string::npos ||
        hitchJson.find("\"render_world_indexed_batch_submit_ms\":0.500") == std::string::npos ||
        hitchJson.find("\"render_other_ms\":1.100") == std::string::npos ||
        hitchJson.find("\"draw_calls\":48") == std::string::npos) {
        outFail = "formatPerfHitchJson should emit stable JSON-style single-frame hitch fields.";
        return false;
    }

    return true;
}

