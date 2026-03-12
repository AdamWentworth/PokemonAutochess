#include <string>

#include "game/runtime/RuntimePerfLogging.h"

bool test_runtime_perf_logging_contract(std::string& outFail) {
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
        line.find("drop=1") == std::string::npos) {
        outFail = "formatPerfLine should emit the expected human-readable perf fields.";
        return false;
    }

    perf.gpuFrameValid = true;
    perf.gpuFrameMs = 8.9f;
    perf.renderBreakdown.worldComposeMs = 1.1f;
    perf.fixedBreakdown.preUpdateMs = 0.2f;
    const std::string json = game::runtime::perf_logging::formatPerfJson(perf);
    if (json.find("[PerfJSON] {") != 0 ||
        json.find("\"gpu_frame_valid\":1") == std::string::npos ||
        json.find("\"gpu_frame_ms\":8.900") == std::string::npos ||
        json.find("\"render_world_compose_ms\":1.100") == std::string::npos ||
        json.find("\"fixed_phase_pre_ms\":0.200") == std::string::npos ||
        json.find("\"fixed_ticks_dropped\":1") == std::string::npos) {
        outFail = "formatPerfJson should emit stable JSON-style perf fields.";
        return false;
    }

    return true;
}
