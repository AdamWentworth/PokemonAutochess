#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

bool fileContainsToken(const std::filesystem::path& path, const std::string& token) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string line;
    while (std::getline(in, line)) {
        if (line.find(token) != std::string::npos) return true;
    }
    return false;
}

} // namespace

bool test_projected_world_scene_seams_contract(std::string& outFail) {
    const std::filesystem::path rendererPath =
        "src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneRenderer.cpp";
    const std::filesystem::path tracePath =
        "src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneTrace.cpp";
    const std::filesystem::path helpersPath =
        "src/game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.cpp";
    const std::filesystem::path boardBenchCachePath =
        "src/game/runtime/shared/projected/world_scene/SharedProjectedBoardBenchGeometryCache.cpp";
    const std::filesystem::path vfxBridgePath =
        "src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldVfxBridges.cpp";
    const std::filesystem::path growlBridgePath =
        "src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldGrowlBridge.cpp";
    const std::filesystem::path tackleBridgePath =
        "src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldTackleBridge.cpp";
    const std::filesystem::path particleBridgePath =
        "src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldParticleVfxBridge.cpp";
    const std::filesystem::path captureBridgePath =
        "src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldCaptureBridge.cpp";
    const std::filesystem::path gameWorldVfxPath =
        "src/game/world/GameWorldVfx.cpp";
    const std::filesystem::path gameWorldSnapshotsPath =
        "src/game/world/GameWorldSharedSnapshots.cpp";
    const std::filesystem::path gameWorldMoveImpactPath =
        "src/game/world/GameWorldMoveImpact.cpp";
    const std::filesystem::path batchStatePath =
        "src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneBatchState.cpp";
    const std::filesystem::path submissionPath =
        "src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneSubmission.cpp";
    const std::vector<std::filesystem::path> retiredSyntheticVfxPaths = {
        "src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldTailFireVfxBridge.cpp",
        "src/game/runtime/shared/projected/world_scene/SharedProjectedUnitWorldSceneTailFireSidecar.cpp",
    };
    const std::vector<std::pair<std::filesystem::path, std::string>> requiredTokens = {
        {rendererPath, "world_scene_trace::traceEnter(args);"},
        {rendererPath, "world_scene_trace::shouldDisableUnit(*args.unit)"},
        {rendererPath, "world_scene_trace::traceFrameSummary("},
        {rendererPath, "world_scene_batch_state::resolveBatchState("},
        {rendererPath, "world_scene_submission::appendWorldSceneInstances("},
        {tracePath, "PAC_TRACE_PROJECTED_WORLD_SCENE"},
        {tracePath, "[ProjectedTrace][WorldScene][Skip] unit="},
        {helpersPath, "board_bench_geometry_cache::appendCachedBoardAndBench3D("},
        {boardBenchCachePath, "struct BoardBenchGeometryCacheKey"},
        {vfxBridgePath, "appendSharedProjectedVfxBridgesSession("},
        {growlBridgePath, "appendSharedGrowlWaveVfxSession("},
        {vfxBridgePath, "appendSharedTackleSmokeVfxSession("},
        {tackleBridgePath, "buildTackleSmokeSnapshot("},
        {tackleBridgePath, "singleRingSnapshot.rings.push_back(ring);"},
        {gameWorldMoveImpactPath, "tackleSmokeVfx.emitAt(impact.position, impact.forward);"},
        {particleBridgePath, "appendSharedParticleVfxSession("},
        {captureBridgePath, "appendSharedCaptureAttemptModelsIfNeededForProjectedWorld("},
        {batchStatePath, "configureGpuClipSkinningBatch("},
        {submissionPath, "persistent::ensureProjectedRenderItem("},
    };
    const std::vector<std::pair<std::filesystem::path, std::string>> forbiddenTokens = {
        {rendererPath, "PAC_TRACE_PROJECTED_WORLD_SCENE"},
        {rendererPath, "appendWorldSceneTraceLineImpl"},
        {helpersPath, "struct BoardBenchGeometryCacheKey"},
        {helpersPath, "appendSharedParticleVfx("},
        {helpersPath, "appendSharedGrowlWaveVfx("},
        {helpersPath, "appendSharedCaptureAttemptModels("},
        {rendererPath, "gpuSkinBatchStateEntries()"},
        {rendererPath, "persistent::ensureProjectedRenderItem("},
        {rendererPath, "fnv1a64Append("},
        {particleBridgePath, "computeUnitProxyExtents("},
        {vfxBridgePath, "buildGrowlWaveSnapshot("},
        {gameWorldVfxPath, "tackleImpactVfx.update(dt);"},
        {gameWorldSnapshotsPath, "tackleImpactVfx.getBurstParticles()"},
        {gameWorldSnapshotsPath, "tackleImpactVfx.getSparkParticles()"},
        {gameWorldMoveImpactPath, "tackleImpactVfx.emitAt("},
    };

    for (const auto& path : retiredSyntheticVfxPaths) {
        if (!std::filesystem::exists(path)) continue;
        outFail = "retired synthetic VFX seam source still exists: " + path.string();
        return false;
    }

    for (const auto& [path, token] : requiredTokens) {
        if (!std::filesystem::exists(path)) {
            outFail = "missing projected world-scene seam source: " + path.string();
            return false;
        }
        if (fileContainsToken(path, token)) continue;
        outFail = "projected world-scene seam contract failed: missing token '" + token +
                  "' in " + path.string();
        return false;
    }

    for (const auto& [path, token] : forbiddenTokens) {
        if (!std::filesystem::exists(path)) {
            outFail = "missing projected world-scene seam source: " + path.string();
            return false;
        }
        if (!fileContainsToken(path, token)) continue;
        outFail = "projected world-scene seam contract failed: unexpected token '" + token +
                  "' in " + path.string();
        return false;
    }

    return true;
}

