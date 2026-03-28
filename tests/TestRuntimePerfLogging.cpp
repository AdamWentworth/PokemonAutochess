#include <string>

#include "game/runtime/loop/RuntimePerfLogging.h"

bool test_runtime_perf_logging_contract(std::string& outFail) {
    if (std::string(game::runtime::perf_logging::terminalLogModeName(
            EngineTerminalLogMode::Performance)) != "Performance" ||
        std::string(game::runtime::perf_logging::terminalLogModeName(
            EngineTerminalLogMode::GrowlVfx)) != "Growl VFX") {
        outFail = "terminalLogModeName should expose stable terminal mode labels.";
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
        .mode = "mesh_quarter_tex",
        .meshPath = "assets/meshes/growl_1255_mesh.glb",
        .texturePath = "assets/textures/moves/growl/Texture3924.png",
        .quarterTextureBake = true,
        .linePass = false,
        .scaleMul = 0.25f,
        .alphaMul = 1.0f,
        .forwardOffset = 0.0f,
    });

    const std::string growlLine = game::runtime::perf_logging::formatGrowlDebugLine(growl);
    if (growlLine.find("[Growl] rings=3") != 0 ||
        growlLine.find("quarter_tex=1") == std::string::npos ||
        growlLine.find("1255:mesh_quarter_tex") == std::string::npos) {
        outFail = "formatGrowlDebugLine should emit the expected Growl summary fields.";
        return false;
    }

    const std::string growlJson = game::runtime::perf_logging::formatGrowlDebugJson(growl);
    if (growlJson.find("[GrowlJSON] {") != 0 ||
        growlJson.find("\"snapshot_available\":1") == std::string::npos ||
        growlJson.find("\"active_rings\":3") == std::string::npos ||
        growlJson.find("\"id\":\"growl_eid_1255\"") == std::string::npos ||
        growlJson.find("\"mesh\":\"assets/meshes/growl_1255_mesh.glb\"") == std::string::npos ||
        growlJson.find("\"quarter_texture_bake\":1") == std::string::npos) {
        outFail = "formatGrowlDebugJson should emit stable JSON-style Growl debug fields.";
        return false;
    }

    return true;
}

