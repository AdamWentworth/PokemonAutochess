#include "game/runtime/session/SessionProjectedWorldView.h"

#include "game/runtime/render_prep/WorldProjection.h"
#include "game/runtime/session/SessionRenderConfig.h"
#include "game/runtime/session/SessionWorldBackdrop.h"
#include "game/runtime/shared/projected/core/SharedProjectedDebugVfx.h"
#include "game/runtime/shared/projected/unit/SharedProjectedUnitRenderer.h"
#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"
#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"
#include "game/world/GameWorld.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <glm/gtc/type_ptr.hpp>

namespace game::runtime::session_projected_world_view {

namespace {

using RenderBuildClock = std::chrono::steady_clock;

void copyVec3ToArray(const glm::vec3& value, std::array<float, 3>& out) {
    out[0] = value.x;
    out[1] = value.y;
    out[2] = value.z;
}

} // namespace

Result appendProjectedWorldView(const Args& args) {
    Result out{};
    if (!args.gameWorld || !args.camera || !args.dataDb || !args.scratch ||
        !args.backendTextureByPath || !args.ensureBackendMeshLoaded ||
        !args.ensureBackendTextureLoaded) {
        return out;
    }

    const float worldCellSize = std::max(0.05f, args.gameWorld->getBoardCellSize());
    const runtime::render_prep_projection::BoardBounds boardBounds =
        runtime::render_prep_projection::computeBoardBounds(
            args.cols,
            args.rows,
            worldCellSize);

    const glm::mat4 view = args.camera->getViewMatrix();
    const glm::mat4 proj = args.camera->getProjectionMatrix();
    const glm::mat4 viewProj = proj * view;
    const glm::mat4 invViewProj = glm::inverse(viewProj);
    if (args.supportsWorldTriangles3D) {
        const float* vp = glm::value_ptr(viewProj);
        std::copy(vp, vp + out.worldViewProj.size(), out.worldViewProj.begin());
        out.hasWorldViewProj = true;
    }

    const glm::mat4 invView = glm::inverse(view);
    glm::vec3 cameraWorldPos(invView[3].x, invView[3].y, invView[3].z);
    if (!std::isfinite(cameraWorldPos.x) ||
        !std::isfinite(cameraWorldPos.y) ||
        !std::isfinite(cameraWorldPos.z)) {
        cameraWorldPos = glm::vec3(0.0f, 6.0f, -6.0f);
    }
    copyVec3ToArray(cameraWorldPos, out.cameraWorldPos);
    copyVec3ToArray(args.camera->getDirection(), out.cameraForward);
    copyVec3ToArray(args.camera->getTarget(), out.cameraTarget);

    const glm::vec4 screenViewport(
        0.0f,
        0.0f,
        static_cast<float>(args.drawableW),
        static_cast<float>(args.drawableH));
    const float line = std::max(1.0f, args.minDim * 0.0019f);

    shared_projected_debug::ProjectedDebugVfxBuilder projectedDebug(
        args.supportsWorldTriangles3D,
        view,
        proj,
        args.drawableH,
        screenViewport,
        args.scratch->worldTriangles,
        args.scratch->world3DTriangles,
        args.scratch->lines);

    auto modelDepthBuffers =
        shared_projected_scene::acquireModelDepthBuffers(12000u);
    auto& modelDepthTris = modelDepthBuffers.modelDepthTris;
    auto& modelDepthWorldTris = modelDepthBuffers.modelDepthWorldTris;
    std::size_t remainingModelTrianglesBudget =
        session_render_config::backendModelTriangleFrameBudget();
    shared_projected_units::PerfStats projectedUnitPerf{};

    std::vector<lgpe_route1_runtime::EncounterGrassInteractor>
        encounterGrassInteractors;
    encounterGrassInteractors.reserve(
        args.gameWorld->getPokemons().size());
    for (const auto& unit : args.gameWorld->getPokemons()) {
        if (!unit.alive || unit.fainting || unit.captureInProgress ||
            !unit.isMoving ||
            unit.airState != AirLocomotionState::Grounded) {
            continue;
        }
        glm::vec3 motion = unit.moveTo - unit.moveFrom;
        motion.y = 0.0f;
        const float motionLength = glm::length(motion);
        if (motionLength <= 0.001f) {
            continue;
        }
        motion /= motionLength;
        encounterGrassInteractors.push_back({
            .worldPosition = {
                unit.position.x,
                unit.position.y,
                unit.position.z},
            .worldMotionDirection = {
                motion.x,
                motion.y,
                motion.z},
            .motionStrength = std::clamp(
                unit.movementSpeed / 1.4f,
                0.35f,
                1.0f)});
    }

    out.worldBackdropComposeMs =
        session_world_backdrop::composeProjectedBackdrop(
            {
                .supportsWorldTriangles3D = args.supportsWorldTriangles3D,
                .supportsWorldIndexedMeshes = args.supportsWorldIndexedMeshes,
                .enableBackdropTiles = args.enableBackdropTiles,
                .enableCanonicalRoute1Environment = true,
                .graphicsQuality = args.graphicsQuality,
                .rows = args.rows,
                .cols = args.cols,
                .benchSlots = args.benchSlots,
                .benchGapCells = args.benchGapCells,
                .worldCellSize = worldCellSize,
                .boardMinX = boardBounds.minX,
                .boardMinZ = boardBounds.minZ,
                .boardMaxX = boardBounds.maxX,
                .boardMaxZ = boardBounds.maxZ,
                .drawableW = args.drawableW,
                .drawableH = args.drawableH,
                .boardX = args.boardX,
                .boardY = args.boardY,
                .boardW = args.boardW,
                .boardH = args.boardH,
                .cellW = args.cellW,
                .cellH = args.cellH,
                .line = line,
                .simulationSeconds =
                    static_cast<float>(args.simNowSec),
                .theme = args.backdropTheme,
                .encounterGrassInteractors =
                    encounterGrassInteractors,
                .route1BackdropTuning =
                    args.route1BackdropTuning
                        ? *args.route1BackdropTuning
                        : session_world_backdrop::defaultRoute1BackdropTuningState(),
                .ensureBackendMeshLoaded = args.ensureBackendMeshLoaded,
                .ensureBackendTextureLoaded = args.ensureBackendTextureLoaded,
            },
            projectedDebug,
            *args.scratch);

    const auto route1Environment =
        args.scratch->route1RuntimeEnvironment;
    if (session_world_backdrop::routeThemeUsesAuthoredRoute1Fallback(
            args.backdropTheme) &&
        route1Environment && route1Environment->loaded()) {
        args.gameWorld->bindGroundHeightResolver(
            route1Environment.get(),
            [route1Environment](
                float worldX,
                float worldZ,
                float& outWorldY) {
                return route1Environment->sampleWorldTerrainHeight(
                    worldX, worldZ, outWorldY);
            });
        // Covers direct roster mutations from state scripts and snapshot
        // loading as well as normal grid-based spawning and movement.
        args.gameWorld->conformPokemonToGround();
    } else {
        args.gameWorld->clearGroundHeightResolver();
    }

    const float boardSurfaceY = 0.006f;
    shared_projected_units::Args projectedUnitArgs;
    projectedUnitArgs.renderer = args.renderer;
    projectedUnitArgs.dataDb = args.dataDb;
    projectedUnitArgs.gameWorld = args.gameWorld;
    projectedUnitArgs.worldCellSize = worldCellSize;
    projectedUnitArgs.minDim = args.minDim;
    projectedUnitArgs.boardSurfaceY = boardSurfaceY;
    projectedUnitArgs.lineThickness = line;
    projectedUnitArgs.supportsWorldTriangles3D = args.supportsWorldTriangles3D;
    projectedUnitArgs.supportsWorldIndexedMeshes = args.supportsWorldIndexedMeshes;
    projectedUnitArgs.characterInkingEnabled = args.characterInkingEnabled;
    projectedUnitArgs.graphicsQuality = args.graphicsQuality;
    projectedUnitArgs.enableGpuClipSkinning = args.enableGpuClipSkinning;
    projectedUnitArgs.tailFireDebugEnabled = args.tailFireDebugEnabled;
    projectedUnitArgs.rendererBackendId = args.renderer ? args.renderer->backendId() : nullptr;
    projectedUnitArgs.hasWorldViewProj = out.hasWorldViewProj;
    projectedUnitArgs.allowPortraitFallback = args.allowPortraitFallback;
    projectedUnitArgs.forcePortraitOverlay = args.forcePortraitOverlay;
    projectedUnitArgs.useLegacyGrowlWaveVfx = args.useLegacyGrowlWaveVfx;
    projectedUnitArgs.useLegacyParticleVfxSnapshotBridge =
        args.useLegacyParticleVfxSnapshotBridge;
    projectedUnitArgs.worldViewProj =
        out.hasWorldViewProj ? out.worldViewProj.data() : nullptr;
    projectedUnitArgs.drawableW = args.drawableW;
    projectedUnitArgs.drawableH = args.drawableH;
    projectedUnitArgs.cameraWorldPos =
        glm::vec3(out.cameraWorldPos[0], out.cameraWorldPos[1], out.cameraWorldPos[2]);
    projectedUnitArgs.projectedDebug = &projectedDebug;
    projectedUnitArgs.projectedRenderItems = &args.scratch->projectedRenderItems;
    projectedUnitArgs.worldSceneRegistry = &args.scratch->worldSceneRegistry;
    projectedUnitArgs.worldSceneFrame = &args.scratch->worldSceneFrame;
    projectedUnitArgs.sharedCaptureAttemptCache = &args.scratch->sharedCaptureAttemptCache;
    projectedUnitArgs.sharedTailFireAnchors = &args.scratch->sharedTailFireAnchors;
    projectedUnitArgs.worldIndexedBatches = &args.scratch->worldIndexedBatches;
    projectedUnitArgs.backendTextureByPath = args.backendTextureByPath;
    projectedUnitArgs.modelDepthTris = &modelDepthTris;
    projectedUnitArgs.modelDepthWorldTris = &modelDepthWorldTris;
    projectedUnitArgs.remainingModelTrianglesBudget = &remainingModelTrianglesBudget;
    projectedUnitArgs.worldQuads = &args.scratch->worldQuads;
    projectedUnitArgs.lines = &args.scratch->lines;
    projectedUnitArgs.textLines = &args.scratch->textLines;
    projectedUnitArgs.sprites = &args.scratch->sprites;
    projectedUnitArgs.worldTriangles = &args.scratch->worldTriangles;
    projectedUnitArgs.world3DTriangles = &args.scratch->world3DTriangles;
    projectedUnitArgs.visibleAnimatedUnitCount = &out.visibleAnimatedUnits;
    projectedUnitArgs.sharedUnitHudCfg = &args.sharedUnitHudCfg;
    projectedUnitArgs.resolveModelMesh =
        [&](const PokemonInstance& unit) -> const runtime::render_model::MeshData* {
            return shared_projected_scene::resolveModelMesh(
                unit,
                *args.dataDb,
                [&](const std::string& modelPath) {
                    return args.ensureBackendMeshLoaded(modelPath);
                });
        };
    projectedUnitArgs.ensureBackendTextureLoaded = args.ensureBackendTextureLoaded;
    projectedUnitArgs.backendModelTriangleLimit = []() {
        return session_render_config::backendModelTriangleLimit();
    };
    projectedUnitArgs.backendModelFullMeshEnabled = []() {
        return session_render_config::backendModelFullMeshEnabled();
    };
    projectedUnitArgs.backendModelFastTexturedPathEnabled = []() {
        return session_render_config::backendModelFastTexturedPathEnabled();
    };
    projectedUnitArgs.backendModelBackfaceCullingEnabled = []() {
        return session_render_config::backendModelBackfaceCullingEnabled();
    };
    projectedUnitArgs.perfStats = &projectedUnitPerf;

    const bool hasActiveCaptureAttempts =
        args.gameWorld->countActiveCaptureAttempts() > 0u;
    if (hasActiveCaptureAttempts) {
        (void)args.scratch->sharedCaptureAttemptCache.refresh(args.gameWorld);
    }

    const auto& boardUnits = args.gameWorld->getPokemons();
    if (!boardUnits.empty()) {
        shared_projected_units::drawProjectedUnits(projectedUnitArgs, boardUnits);
    }
    const auto& benchUnits = args.gameWorld->getBenchPokemons();
    if (!benchUnits.empty()) {
        shared_projected_units::drawProjectedUnits(projectedUnitArgs, benchUnits);
    }

    const bool capturePrewarmRequested =
        args.gameWorld->getSelectedItem() == "pokeball" ||
        args.gameWorld->getItemCount("pokeball") > 0;
    if (hasActiveCaptureAttempts ||
        capturePrewarmRequested ||
        !args.scratch->sharedCaptureAttemptCache.snaps.empty()) {
        (void)shared_projected_scene::appendSharedCaptureAttemptModelsIfNeededForProjectedWorld(
            args.renderer,
            args.gameWorld,
            args.supportsWorldIndexedMeshes,
            out.hasWorldViewProj,
            args.drawableW,
            args.drawableH,
            worldCellSize,
            out.hasWorldViewProj ? out.worldViewProj.data() : nullptr,
            glm::vec3(out.cameraWorldPos[0], out.cameraWorldPos[1], out.cameraWorldPos[2]),
            args.scratch->sharedCaptureAttemptCache,
            args.scratch->worldIndexedBatches,
            *args.backendTextureByPath,
            [&](const std::string& path) {
                return args.ensureBackendMeshLoaded(path);
            },
            [&](const std::string& path) {
                return args.ensureBackendTextureLoaded(path, false);
            });
    }

    const auto worldVfxStart = RenderBuildClock::now();
    shared_projected_scene::appendSharedProjectedVfxBridgesSession(
        args.useLegacyParticleVfxSnapshotBridge,
        args.useLegacyGrowlWaveVfx,
        args.supportsWorldIndexedMeshes,
        out.hasWorldViewProj,
        args.useExactTailFireCpuPath,
        args.tailFireDebugEnabled,
        args.gameWorld,
        viewProj,
        invViewProj,
        glm::vec3(out.cameraWorldPos[0], out.cameraWorldPos[1], out.cameraWorldPos[2]),
        args.drawableW,
        args.drawableH,
        worldCellSize,
        args.simNowSec,
        line,
        args.scratch->sharedTailFireAnchors,
        *args.backendTextureByPath,
        args.scratch->worldIndexedBatches,
        projectedDebug,
        [&](const std::string& meshPath) {
            return args.ensureBackendMeshLoaded(meshPath);
        },
        args.ensureBackendTextureLoaded);
    const auto worldVfxEnd = RenderBuildClock::now();
    out.worldVfxBridgeMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(worldVfxEnd - worldVfxStart).count());

    const auto depthFlushStart = RenderBuildClock::now();
    shared_projected_scene::flushModelDepthBuffers(
        modelDepthTris,
        modelDepthWorldTris,
        args.scratch->worldTriangles,
        args.scratch->world3DTriangles);
    const auto depthFlushEnd = RenderBuildClock::now();
    out.worldDepthFlushMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(depthFlushEnd - depthFlushStart).count());

    out.projectedUnitsMs = static_cast<float>(projectedUnitPerf.totalMs);
    out.projectedPoseEvalMs = static_cast<float>(projectedUnitPerf.poseEvalMs);
    out.projectedModelMs = static_cast<float>(projectedUnitPerf.modelRenderMs);
    out.projectedModelPrepMs = static_cast<float>(projectedUnitPerf.modelPrepMs);
    out.projectedModelGeometryMs = static_cast<float>(projectedUnitPerf.modelGeometryMs);
    out.projectedOverlayMs = static_cast<float>(projectedUnitPerf.overlayMs);
    out.projectedUnitsProcessed = projectedUnitPerf.unitsProcessed;
    out.projectedModelUnits = projectedUnitPerf.modelUnits;
    out.projectedClipSkinnedUnits = projectedUnitPerf.clipSkinnedUnits;
    out.projectedSharedRigidBatches = projectedUnitPerf.sharedRigidBatches;
    out.projectedGpuClipSkinBatches = projectedUnitPerf.gpuClipSkinBatches;
    out.projectedGpuClipPaletteBatches = projectedUnitPerf.gpuClipPaletteBatches;
    out.projectedCpuRewriteBatches = projectedUnitPerf.cpuRewriteBatches;
    out.projectedIndexedBatchesQueued = projectedUnitPerf.indexedBatchesQueued;
    return out;
}

} // namespace game::runtime::session_projected_world_view

