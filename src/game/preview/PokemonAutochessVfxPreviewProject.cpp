#include "game/preview/PokemonAutochessVfxPreviewProject.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/core/Paths.h"
#include "engine/core/Environment.h"
#include "engine/render/BoardRenderer.h"
#include "engine/render/Camera3D.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "game/GameConfig.h"
#include "game/config/AttackAnimConfigLoader.h"
#include "game/config/GameDataDb.h"
#include "game/preview/PreviewAnimatedModelPresentation.h"
#include "game/config/PokemonConfigLoader.h"
#include "game/preview/PreviewPokemonVisual.h"
#include "game/preview/PreviewSceneUtils.h"
#include "game/preview/PreviewTailFireBridge.h"
#include "game/preview/effects/GrowlPreviewEffect.h"
#include "game/preview/effects/GameplayParticlePreviewEffect.h"
#include "game/preview/effects/LeechSeedPreviewEffect.h"
#include "game/preview/PreviewBodyRenderRouting.h"
#include "game/preview/effects/ScratchPreviewEffect.h"
#include "game/preview/effects/TacklePreviewEffect.h"
#include "game/runtime/render_prep/UnitVisuals.h"
#include "game/runtime/render_prep/ProceduralPose.h"
#include "game/runtime/render_prep/WorldProxyGeometry.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/session/SessionRenderConfig.h"
#include "game/runtime/session/SessionRenderScratch.h"
#include "game/runtime/session/SessionTextureCache.h"
#include "game/runtime/session/SessionWorldBackdrop.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/projected/core/SharedProjectedBodyPresentation.h"
#include "game/runtime/shared/projected/core/SharedPreviewBodyPresentationPath.h"
#include "game/runtime/shared/projected/unit/SharedProjectedUnitModelRenderer.h"
#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireCoordinator.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFirePlaybackPolicy.h"
#include "game/runtime/shared/world/SharedWorldContentSubmit.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "game/world/MoveImpactMath.h"
#include "game/world/MoveImpactRouting.h"

namespace game::preview {

namespace {

float computeProjectedUnitSizePx(const game::runtime::shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug,
                                 const glm::vec3& worldPos,
                                 float worldCellSize,
                                 float minDim) {
    float cx = 0.0f;
    float cy = 0.0f;
    float cz = 0.0f;
    float sx = 0.0f;
    float sy = 0.0f;
    float sz = 0.0f;
    const bool hasCenter = projectedDebug.projectWorld(worldPos, cx, cy, cz);
    const bool hasCellX = projectedDebug.projectWorld(
        worldPos + glm::vec3(worldCellSize, 0.0f, 0.0f), sx, sy, sz);
    if (hasCenter && hasCellX) {
        const float cellPx = glm::length(glm::vec2(sx - cx, sy - cy));
        if (std::isfinite(cellPx) && cellPx >= 8.0f) {
            return std::clamp(cellPx * 0.75f, 10.0f, 84.0f);
        }
    }
    return std::max(14.0f, minDim * 0.035f);
}

std::string previewBodyPathLabel(
    game::runtime::shared_preview_body_presentation_path::PreviewBodyPathDecision decision) {
    namespace preview_body = game::runtime::shared_preview_body_presentation_path;
    switch (decision) {
    case preview_body::PreviewBodyPathDecision::ProjectedWorldScene:
        return "projected_world_scene";
    case preview_body::PreviewBodyPathDecision::ProjectedIndexedScratch:
        return "projected_indexed_scratch";
    case preview_body::PreviewBodyPathDecision::DirectAnimatedFallback:
    default:
        return "direct_fallback";
    }
}

bool previewBodyPathTraceEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_TRACE_PREVIEW_BODY_PATH");
        if (!env.has_value()) return false;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

bool previewCharacterInkingEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_PREVIEW_CHARACTER_INKING");
        if (!env.has_value()) return false;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

void setPreviewVisualSpeciesLocal(game::preview::PreviewPokemonVisual& visual,
                                  std::string_view speciesName) {
    if (speciesName.empty() || visual.speciesName == speciesName) return;
    visual = game::preview::PreviewPokemonVisual{std::string(speciesName)};
}

} // namespace

struct PokemonAutochessVfxPreviewProject::Impl {
    enum class PendingReplayAction {
        None,
        Replay,
        Reload,
    };

    struct BackendMeshCacheEntry {
        bool attemptedLoad = false;
        bool reportedFailure = false;
        game::runtime::render_model::MeshData mesh;
        std::string error;
    };

    struct PreviewBodyDebugState {
        std::string speciesName;
        bool builtProjectedScratch = false;
        bool haveDirectBodySample = false;
        game::runtime::shared_preview_body_presentation_path::PreviewBodyPathSummary pathSummary{};
    };

    GameConfigData gameConfig;
    bool gameConfigLoaded = false;
    PokemonConfigLoader pokemonConfig;
    bool pokemonConfigLoaded = false;
    MovesConfigLoader movesConfig;
    bool movesConfigLoaded = false;
    AttackAnimConfigLoader attackAnimConfig;
    bool attackAnimConfigLoaded = false;
    PreviewCombatTuning combatTuning{};
    bool combatTuningLoaded = false;
    GameDataDb previewDataDb;
    bool previewDataDbReady = false;
    PreviewPokemonVisual attackerVisual{"charmander"};
    PreviewPokemonVisual targetVisual{"bulbasaur"};
    TailFireVFXConfig tailFireConfig{};
    bool tailFireConfigLoaded = false;
    double tailFireSimNowSec = 0.0;
    std::size_t activeEffectIndex = 0u;
    std::size_t activeRigIndex = 0u;
    PendingReplayAction pendingReplayAction = PendingReplayAction::None;
    float pendingReplayTriggerAnimTimeSec = 0.0f;
    std::unique_ptr<OpenGLRenderBackend> backendRenderer;
    int backendSurfaceWidth = 0;
    int backendSurfaceHeight = 0;
    game::runtime::session_texture_cache::TextureCache backendTextureByPath;
    std::unordered_map<std::string, BackendMeshCacheEntry> backendMeshByModelPath;
    game::runtime::session_render_scratch::RenderScratch backdropScratch;
    game::runtime::session_render_scratch::RenderScratch modelScratch;
    game::runtime::session_render_scratch::RenderScratch tailFireScratch;
    std::vector<game::runtime::shared_projected_scene::DepthTri> modelDepthTris;
    std::vector<game::runtime::shared_projected_scene::DepthWorldTri> modelDepthWorldTris;
    PreviewBodyDebugState lastAttackerBodyDebug{};
    PreviewBodyDebugState lastTargetBodyDebug{};
    std::string lastAttackerBodyTraceLine;
    std::string lastTargetBodyTraceLine;

    void applyEffectPreviewSpecies(const engine::tools::vfx_preview::IVfxPreviewEffect& effect) {
        const auto actors = effect.previewActors();
        setPreviewVisualSpeciesLocal(attackerVisual, actors.emitterActorId);
        setPreviewVisualSpeciesLocal(targetVisual, actors.targetActorId);
    }

    void updateEffectImpactPoint(const engine::tools::vfx_preview::IVfxPreviewEffect* effect,
                                 engine::tools::vfx_preview::PreviewSceneState& scene) {
        scene.useCustomImpactPoint = false;
        scene.impactPoint = scene.target;
        if (!effect || !effect->wantsTargetSurfaceImpactPoint()) return;

        ensurePokemonConfigLoaded();
        attackerVisual.ensureLoaded(pokemonConfig);
        targetVisual.ensureLoaded(pokemonConfig);
        if (!attackerVisual.valid || !targetVisual.valid) return;

        const glm::vec3 casterPos(scene.emitter.x, 0.0f, scene.emitter.z);
        const glm::vec3 targetPos(scene.target.x, 0.0f, scene.target.z);
        const float attackerYaw = computeYawDegreesFromForward(targetPos - casterPos);
        const float targetYaw = computeYawDegreesFromForward(casterPos - targetPos);

        const PokemonInstance attackerUnit =
            makePreviewRuntimeUnit(attackerVisual, casterPos, attackerYaw, PokemonSide::Player);
        const PokemonInstance targetUnit =
            makePreviewRuntimeUnit(targetVisual, targetPos, targetYaw, PokemonSide::Enemy);

        const auto* attackerMesh = ensureBackendMeshLoaded(attackerVisual.modelPath);
        const auto* targetMesh = ensureBackendMeshLoaded(targetVisual.modelPath);
        const MoveImpactSurfacePoint impact =
            computeTargetSurfaceImpactPoint(targetUnit, &attackerUnit, targetMesh, attackerMesh);
        scene.impactPoint = impact.position;
        scene.useCustomImpactPoint = true;
    }

    void submitScratch(const Camera3D& camera,
                       int surfaceWidth,
                       int surfaceHeight,
                       game::runtime::session_render_scratch::RenderScratch& scratch,
                       bool renderProjectedLines,
                       bool indexedOnlyWhenAvailable = false);
    void appendBackdropTiles(const Camera3D& camera,
                             int surfaceWidth,
                             int surfaceHeight);
    bool buildProjectedModelScratch(const Camera3D& camera,
                                    int surfaceWidth,
                                    int surfaceHeight,
                                    const PreviewPokemonVisual& visual,
                                    const glm::vec3& worldPos,
                                    float yawDeg,
                                    PokemonSide side);
    void renderPreviewUnit(const Camera3D& camera,
                           int surfaceWidth,
                           int surfaceHeight,
                           PreviewPokemonVisual& visual,
                           const glm::vec3& worldPos,
                           float yawDeg,
                           PokemonSide side);
    void renderTailFireBillboards(const Camera3D& camera,
                                  int surfaceWidth,
                                  int surfaceHeight,
                                  const PreviewPokemonVisual& visual,
                                  const glm::vec3& worldPos,
                                  float yawDeg,
                                  PokemonSide side) {
        if (!backendRenderer || !visual.valid) return;
        ensureTailFireConfigLoaded();
        renderPreviewTailFire(
            {
                .camera = &camera,
                .renderer = backendRenderer.get(),
                .surfaceWidth = surfaceWidth,
                .surfaceHeight = surfaceHeight,
                .worldCellSize = boardCellSize(),
                .simNowSec = tailFireSimNowSec,
                .fallbackConfig = tailFireConfigLoaded ? &tailFireConfig : nullptr,
                .visual = &visual,
                .worldPos = worldPos,
                .yawDeg = yawDeg,
                .side = side,
                .backendTextureByPath = &backendTextureByPath,
                .modelScratch = &modelScratch,
                .tailFireScratch = &tailFireScratch,
                .buildProjectedModelScratch =
                    [&](const Camera3D& bridgeCamera,
                        int bridgeSurfaceWidth,
                        int bridgeSurfaceHeight,
                        const PreviewPokemonVisual& bridgeVisual,
                        const glm::vec3& bridgeWorldPos,
                        float bridgeYawDeg,
                        PokemonSide bridgeSide) {
                        return buildProjectedModelScratch(
                            bridgeCamera,
                            bridgeSurfaceWidth,
                            bridgeSurfaceHeight,
                            bridgeVisual,
                            bridgeWorldPos,
                            bridgeYawDeg,
                            bridgeSide);
                    },
                .ensureBackendTextureLoaded =
                    [&](const std::string& texturePath, bool flipVertical)
                        -> game::runtime::SharedBackendTextureCacheEntry* {
                        return ensureBackendTextureLoaded(texturePath, flipVertical);
                    },
                .submitScratch =
                    [&](const Camera3D& bridgeCamera,
                        int bridgeSurfaceWidth,
                        int bridgeSurfaceHeight,
                        game::runtime::session_render_scratch::RenderScratch& scratch,
                        bool renderProjectedLines,
                        bool indexedOnlyWhenAvailable) {
                        submitScratch(
                            bridgeCamera,
                            bridgeSurfaceWidth,
                            bridgeSurfaceHeight,
                            scratch,
                            renderProjectedLines,
                            indexedOnlyWhenAvailable);
                    },
            });
    }
    void maybeTracePreviewBodyPath(const PreviewBodyDebugState& state,
                                   std::string& lastTraceLine) const {
        if (!previewBodyPathTraceEnabled()) return;
        std::string line =
            "[PreviewBodyPath] species=" + state.speciesName +
            " decision=" + previewBodyPathLabel(state.pathSummary.decision) +
            " builtScratch=" + std::to_string(state.builtProjectedScratch ? 1 : 0) +
            " directSample=" + std::to_string(state.haveDirectBodySample ? 1 : 0) +
            " ws=" + std::to_string(state.pathSummary.worldSceneDrawClassCount) +
            " idx=" + std::to_string(state.pathSummary.worldIndexedBatchCount) +
            " litBody=" + std::to_string(state.pathSummary.litTexturedIndexedBodyBatchCount) +
            " fire=" + std::to_string(state.pathSummary.authoredFireBatchCount) +
            " allowIndexed=" + std::to_string(state.pathSummary.allowIndexedScratchPath ? 1 : 0);
        if (line == lastTraceLine) return;
        lastTraceLine = line;
        std::cout << line << "\n" << std::flush;
    }

    float computeAttackSpeedFactor(const PreviewPokemonVisual& visual) {
        ensureCombatTuningLoaded();
        const float denom = (combatTuning.speedBaseline != 0.0f) ? combatTuning.speedBaseline : 1.0f;
        float speedFactor = visual.runtimeLikeUnit.movementSpeed / denom;
        speedFactor *= combatTuning.attackSpeedScale;
        return std::clamp(speedFactor, combatTuning.speedMin, combatTuning.speedMax);
    }

    float computeReplayWindowSec(const PreviewPokemonVisual& visual,
                                 const engine::tools::vfx_preview::PreviewCasterAnimationRequest& request) {
        ensureMovesConfigLoaded();
        ensureAttackAnimConfigLoaded();
        ensureCombatTuningLoaded();

        const std::string moveLower = lowerCopy(std::string(request.move));
        const std::string kindLower = lowerCopy(std::string(request.kind));
        const MoveData* moveData = movesConfig.getMove(moveLower);
        const float speedFactor = computeAttackSpeedFactor(visual);
        if (speedFactor <= 0.0001f) {
            return std::max(0.05f, visual.runtimeLikeUnit.attackDurationSec);
        }

        if (kindLower == "charged") {
            float cd = std::max(0.05f, moveData ? moveData->cooldownSec : 0.8f);
            cd = (cd * combatTuning.chargedCdMult) / speedFactor;
            const float minReq =
                attackAnimConfig.getMinRequestSec(visual.speciesName, kindLower, moveLower);
            cd = std::max(cd, std::max(0.0f, minReq) / speedFactor);
            return std::max(0.05f, cd);
        }

        return std::max(0.05f, visual.runtimeLikeUnit.attackDurationSec);
    }

    void ensureTailFireConfigLoaded() {
        if (!tailFireConfigLoaded) {
            tailFireConfig =
                game::runtime::shared_tail_fire_coordinator::resolvePrimaryPlaybackConfig();
            tailFireConfig.useUnitScaleChain = true;
            tailFireConfigLoaded = true;
        }
    }
    void resetTailFirePreview() {
        tailFireSimNowSec = 0.0;
    }

    void ensureGameConfigLoaded() {
        if (gameConfigLoaded) return;
        gameConfig = GameConfig::load();
        gameConfigLoaded = true;
    }

    float boardCellSize() {
        ensureGameConfigLoaded();
        return std::max(0.05f, gameConfig.cellSize);
    }

    void ensurePokemonConfigLoaded() {
        if (pokemonConfigLoaded) return;
        pokemonConfigLoaded = pokemonConfig.loadConfig(engine::paths::data("config/pokemon_config.json"));
    }

    void ensureAttackAnimConfigLoaded() {
        if (attackAnimConfigLoaded) return;
        attackAnimConfigLoaded =
            attackAnimConfig.loadConfig(engine::paths::data("config/attack_anim_config.json"));
    }

    void ensureMovesConfigLoaded() {
        if (movesConfigLoaded) return;
        movesConfigLoaded = movesConfig.loadConfig(engine::paths::data("config/moves_config.json"));
    }

    void ensureCombatTuningLoaded() {
        if (combatTuningLoaded) return;
        combatTuning = loadPreviewCombatTuningFromConfig();
        combatTuningLoaded = true;
    }

    void ensurePreviewDataDbReady() {
        if (previewDataDbReady) return;
        ensurePokemonConfigLoaded();
        ensureMovesConfigLoaded();
        ensureAttackAnimConfigLoaded();
        previewDataDb.pokemon = pokemonConfig;
        previewDataDb.moves = movesConfig;
        previewDataDb.attackAnims = attackAnimConfig;
        previewDataDbReady = true;
    }

    void syncBackendSurface(int width, int height) {
        width = std::max(1, width);
        height = std::max(1, height);
        if (!backendRenderer) {
            backendRenderer = std::make_unique<OpenGLRenderBackend>();
        }
        if (width == backendSurfaceWidth && height == backendSurfaceHeight) return;
        backendSurfaceWidth = width;
        backendSurfaceHeight = height;
        backendRenderer->onResize(width, height);
    }

    game::runtime::SharedBackendTextureCacheEntry* ensureBackendTextureLoaded(
        const std::string& texturePath,
        bool flipVertical = false) {
        return game::runtime::session_texture_cache::ensureTextureLoaded(
            backendTextureByPath,
            texturePath,
            flipVertical);
    }

    game::runtime::render_model::MeshData* ensureBackendMeshLoaded(const std::string& modelPath) {
        auto& cacheEntry = backendMeshByModelPath[modelPath];
        if (!cacheEntry.attemptedLoad) {
            cacheEntry.attemptedLoad = true;
            std::string err;
            if (!game::runtime::render_model::loadMeshFromCache(modelPath, cacheEntry.mesh, &err)) {
                cacheEntry.error = std::move(err);
                cacheEntry.mesh = {};
            }
        }

        if (!cacheEntry.error.empty()) {
            if (!cacheEntry.reportedFailure) {
                std::cout << "[PAC_VfxPreviewer] Unable to load cached mesh '" << modelPath
                          << "' (" << cacheEntry.error << ")\n";
                cacheEntry.reportedFailure = true;
            }
            return nullptr;
        }
        if (cacheEntry.mesh.vertices.empty() || cacheEntry.mesh.indices.size() < 3u) {
            return nullptr;
        }
        return &cacheEntry.mesh;
    }
};

void PokemonAutochessVfxPreviewProject::Impl::submitScratch(
    const Camera3D& camera,
    int surfaceWidth,
    int surfaceHeight,
    game::runtime::session_render_scratch::RenderScratch& scratch,
    bool renderProjectedLines,
    bool indexedOnlyWhenAvailable) {
    const glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    const bool preferIndexedOnly =
        indexedOnlyWhenAvailable && !scratch.worldIndexedBatches.empty();

    const auto worldSceneView = game::runtime::shared_world_scene::buildWorldSceneView(
        scratch.worldSceneRegistry,
        glm::value_ptr(viewProj),
        surfaceWidth,
        surfaceHeight,
        glm::value_ptr(camera.getPosition()),
        glm::value_ptr(camera.getDirection()),
        glm::value_ptr(camera.getTarget()));
    std::vector<IRenderBackend::WorldTriangle> noWorld3DTriangles;
    game::runtime::shared_world_content_submit::submitOpaqueAndIndexedWorldContent(
        {
            .renderer = backendRenderer.get(),
            .camera = &camera,
            .drawableW = surfaceWidth,
            .drawableH = surfaceHeight,
            .hasWorldViewProj = true,
            .supportsWorldTriangles3D =
                !preferIndexedOnly && backendRenderer->supportsWorldTriangles3D(),
            .supportsWorldIndexedMeshes = backendRenderer->supportsWorldIndexedMeshes(),
            .worldViewProj = glm::value_ptr(viewProj),
            .worldBackgroundQuads = &scratch.worldBackgroundQuads,
            .world3DTriangles =
                preferIndexedOnly ? &noWorld3DTriangles : &scratch.world3DTriangles,
            .worldSceneView = &worldSceneView,
            .worldSceneFrame = &scratch.worldSceneFrame,
            .worldIndexedBatches = &scratch.worldIndexedBatches,
        });
    if (!preferIndexedOnly && !scratch.worldTriangles.empty()) {
        backendRenderer->drawDebugTriangles(
            scratch.worldTriangles.data(),
            scratch.worldTriangles.size(),
            surfaceWidth,
            surfaceHeight);
    }
    if (!preferIndexedOnly && !scratch.worldQuads.empty()) {
        backendRenderer->drawDebugQuads(
            scratch.worldQuads.data(),
            scratch.worldQuads.size(),
            surfaceWidth,
            surfaceHeight);
    }
    if (renderProjectedLines && !scratch.lines.empty()) {
        backendRenderer->drawDebugLines(
            scratch.lines.data(),
            scratch.lines.size(),
            surfaceWidth,
            surfaceHeight);
    }
}

void PokemonAutochessVfxPreviewProject::Impl::appendBackdropTiles(
    const Camera3D& camera,
    int surfaceWidth,
    int surfaceHeight) {
    auto& scratch = backdropScratch;
    game::runtime::session_render_scratch::ensureCapacity(scratch);
    game::runtime::session_render_scratch::beginFrame(scratch, true, backendRenderer.get());

    const glm::mat4 view = camera.getViewMatrix();
    const glm::mat4 proj = camera.getProjectionMatrix();
    const glm::vec4 viewport(
        0.0f,
        0.0f,
        static_cast<float>(surfaceWidth),
        static_cast<float>(surfaceHeight));

    game::runtime::shared_projected_debug::ProjectedDebugVfxBuilder projectedDebug(
        true,
        view,
        proj,
        surfaceHeight,
        viewport,
        scratch.worldTriangles,
        scratch.world3DTriangles,
        scratch.lines);

    const float cellSize = boardCellSize();
    const float boardMinX = previewBoardOriginX(cellSize) - cellSize * 0.5f;
    const float boardMinZ = previewBoardOriginZ(cellSize) - cellSize * 0.5f;
    const float boardMaxX = boardMinX + static_cast<float>(kPreviewBoardCols) * cellSize;
    const float boardMaxZ = boardMinZ + static_cast<float>(kPreviewBoardRows) * cellSize;
    const float line = std::max(1.0f, std::min(surfaceWidth, surfaceHeight) * 0.0019f);

    (void)game::runtime::session_world_backdrop::composeProjectedBackdrop(
        {
            .supportsWorldTriangles3D = true,
            .supportsWorldIndexedMeshes = true,
            .enableBackdropTiles = true,
            .graphicsQuality = 3,
            .rows = kPreviewBoardRows,
            .cols = kPreviewBoardCols,
            .benchSlots = kPreviewBenchSlots,
            .worldCellSize = cellSize,
            .boardMinX = boardMinX,
            .boardMinZ = boardMinZ,
            .boardMaxX = boardMaxX,
            .boardMaxZ = boardMaxZ,
            .drawableW = surfaceWidth,
            .drawableH = surfaceHeight,
            .boardX = 0.0f,
            .boardY = 0.0f,
            .boardW = static_cast<float>(surfaceWidth),
            .boardH = static_cast<float>(surfaceHeight),
            .cellW = static_cast<float>(surfaceWidth) / static_cast<float>(kPreviewBoardCols),
            .cellH = static_cast<float>(surfaceHeight) / static_cast<float>(kPreviewBoardRows),
            .line = line,
            .theme = game::runtime::session_world_backdrop::ArenaBackdropTheme::Default,
            .ensureBackendTextureLoaded =
                [&](const std::string& texturePath, bool flipVertical)
                    -> game::runtime::SharedBackendTextureCacheEntry* {
                    return ensureBackendTextureLoaded(texturePath, flipVertical);
                },
        },
        projectedDebug,
        scratch);
}

bool PokemonAutochessVfxPreviewProject::Impl::buildProjectedModelScratch(
    const Camera3D& camera,
    int surfaceWidth,
    int surfaceHeight,
    const PreviewPokemonVisual& visual,
    const glm::vec3& worldPos,
    float yawDeg,
    PokemonSide side) {
    if (!visual.valid || !visual.model) return false;

    ensurePreviewDataDbReady();
    game::runtime::render_model::MeshData* mesh =
        ensureBackendMeshLoaded(visual.modelPath);
    if (!mesh) return false;

    auto& scratch = modelScratch;
    game::runtime::session_render_scratch::ensureCapacity(scratch);
    game::runtime::session_render_scratch::beginFrame(scratch, true, backendRenderer.get());

    modelDepthTris.clear();
    modelDepthWorldTris.clear();
    modelDepthTris.reserve(12000u);
    modelDepthWorldTris.reserve(12000u);

    const glm::mat4 view = camera.getViewMatrix();
    const glm::mat4 proj = camera.getProjectionMatrix();
    const glm::vec4 viewport(
        0.0f,
        0.0f,
        static_cast<float>(surfaceWidth),
        static_cast<float>(surfaceHeight));
    game::runtime::shared_projected_debug::ProjectedDebugVfxBuilder projectedDebug(
        true,
        view,
        proj,
        surfaceHeight,
        viewport,
        scratch.worldTriangles,
        scratch.world3DTriangles,
        scratch.lines);

    PokemonInstance unit = visual.runtimeLikeUnit;
    unit = makePreviewRuntimeUnit(visual, worldPos, yawDeg, side);

    game::runtime::shared_backend_pose::PoseEval scenePose{};
    if (!preview_animated_model_presentation::buildScenePose(visual, *mesh, scenePose)) {
        scenePose = game::runtime::shared_backend_pose::evaluateScenePose(*mesh, unit);
    }
    const bool enableGpuClipSkinning =
        game::runtime::session_render_config::backendGpuClipSkinningEnabled(
            backendRenderer.get());

    IRenderBackend::DebugQuad tint{};
    game::runtime::render_prep_units::applyWorldUnitTint(tint, unit);

    const float minDim = static_cast<float>(std::min(surfaceWidth, surfaceHeight));
    const float cellSize = boardCellSize();
    const bool exactClipMotionPreview = visual.previewUseExactClipMotion;
    const auto previewPose =
        game::runtime::render_prep_pose::computeProceduralPose(unit, cellSize);
    const bool applyProceduralAttackMotion =
        !exactClipMotionPreview &&
        previewPose.activeAttackWindow &&
        shouldApplyProceduralAttackLunge(unit.activeAttackMoveName);
    const glm::vec3 attackOffset = applyProceduralAttackMotion
        ? (game::runtime::render_prep_proxy::yawForward(unit.rotation.y) *
           previewPose.attackLunge)
        : glm::vec3(0.0f);
    const float attackPulse = applyProceduralAttackMotion ? previewPose.attackPulse : 1.0f;
    const glm::vec3 proxyCenter = worldPos + attackOffset;
    const glm::vec3 coarseWorldPos =
        proxyCenter + glm::vec3(0.0f, std::max(0.2f, cellSize * 0.22f), 0.0f);
    const float unitSizePx =
        computeProjectedUnitSizePx(projectedDebug, coarseWorldPos, cellSize, minDim);
    std::size_t remainingModelTrianglesBudget = 60000u;

    game::runtime::shared_projected_unit_models::PerfBreakdown perf{};
    const auto result =
        game::runtime::shared_projected_body_presentation::buildProjectedBodyPresentation(
            game::runtime::shared_projected_unit_models::Args{
            .renderer = backendRenderer.get(),
            .dataDb = &previewDataDb,
            .unit = &unit,
            .pose = &previewPose,
            .meshForUnit = mesh,
            .scenePose = &scenePose,
            .backendId = backendRenderer ? backendRenderer->backendId() : nullptr,
            .scenePoseReady = true,
            .enableClipSkinning = true,
            .enableGpuClipSkinning = enableGpuClipSkinning,
            .tint = &tint,
            .worldCellSize = cellSize,
            .boardSurfaceY = 0.006f,
            .unitSize = unitSizePx,
            .animPitch = 0.0f,
            .animYaw = yawDeg,
            .animRoll = 0.0f,
            .attackPulse = attackPulse,
            .materialTimeSec = unit.animTimeSec,
            .materialAnimationIndex = unit.activeAnimIndex,
            .materialAnimationTimeSec = unit.animTimeSec,
            .renderVisualScale = 1.0f,
            .renderCaptureScale = 1.0f,
            .captureVisualTintStrength = 0.0f,
            .modelFadeAlpha = 1.0f,
            .captureTintColor = glm::vec3(1.0f),
            .proxyCenter = proxyCenter,
            .cameraWorldPos = camera.getPosition(),
            .supportsWorldTriangles3D =
                backendRenderer && backendRenderer->supportsWorldTriangles3D(),
            .supportsWorldIndexedMeshes =
                backendRenderer && backendRenderer->supportsWorldIndexedMeshes(),
            .characterInkingEnabled = previewCharacterInkingEnabled(),
            .graphicsQuality = 3,
            .projectedDebug = &projectedDebug,
            .projectedRenderItems = &scratch.projectedRenderItems,
            .worldSceneRegistry = &scratch.worldSceneRegistry,
            .worldSceneFrame = &scratch.worldSceneFrame,
            .sharedTailFireAnchors = &scratch.sharedTailFireAnchors,
            .worldIndexedBatches = &scratch.worldIndexedBatches,
            .backendTextureByPath = &backendTextureByPath,
            .modelDepthTris = &modelDepthTris,
            .modelDepthWorldTris = &modelDepthWorldTris,
            .remainingModelTrianglesBudget = &remainingModelTrianglesBudget,
            .world3DTriangles = &scratch.world3DTriangles,
            .ensureBackendTextureLoaded =
                [&](const std::string& texturePath, bool flipVertical)
                    -> game::runtime::SharedBackendTextureCacheEntry* {
                    return ensureBackendTextureLoaded(texturePath, flipVertical);
                },
            .backendModelTriangleLimit = []() {
                return game::runtime::session_render_config::backendModelTriangleLimit();
            },
            .backendModelFullMeshEnabled = []() {
                return game::runtime::session_render_config::backendModelFullMeshEnabled();
            },
            .backendModelFastTexturedPathEnabled = []() {
                return game::runtime::session_render_config::backendModelFastTexturedPathEnabled();
            },
            .backendModelBackfaceCullingEnabled = []() {
                return game::runtime::session_render_config::backendModelBackfaceCullingEnabled();
            },
            .perfBreakdown = &perf,
        });

    game::runtime::shared_projected_scene::flushModelDepthBuffers(
        modelDepthTris,
        modelDepthWorldTris,
        scratch.worldTriangles,
        scratch.world3DTriangles);

    return result.producedScratch;
}

void PokemonAutochessVfxPreviewProject::Impl::renderPreviewUnit(
    const Camera3D& camera,
    int surfaceWidth,
    int surfaceHeight,
    PreviewPokemonVisual& visual,
    const glm::vec3& worldPos,
    float yawDeg,
    PokemonSide side) {
    const PokemonInstance previewUnit =
        makePreviewRuntimeUnit(visual, worldPos, yawDeg, side);
    const bool exactClipMotionPreview = visual.previewUseExactClipMotion;
    const PreviewBodyRenderRouting renderRouting =
        resolvePreviewBodyRenderRouting(visual.speciesName, exactClipMotionPreview);
    const auto previewPose =
        game::runtime::render_prep_pose::computeProceduralPose(previewUnit, boardCellSize());
    const bool applyProceduralAttackMotion =
        !exactClipMotionPreview &&
        previewPose.activeAttackWindow &&
        shouldApplyProceduralAttackLunge(previewUnit.activeAttackMoveName);
    const glm::vec3 attackOffset = applyProceduralAttackMotion
        ? (game::runtime::render_prep_proxy::yawForward(previewUnit.rotation.y) *
           previewPose.attackLunge)
        : glm::vec3(0.0f);
    const float attackPulse = applyProceduralAttackMotion ? previewPose.attackPulse : 1.0f;

    preview_animated_model_presentation::DirectBodySample bodySample;
    const bool haveDirectBodySample =
        preview_animated_model_presentation::buildDirectBodySample(
            visual,
            worldPos + attackOffset,
            yawDeg,
            bodySample,
            0.006f,
            attackPulse);

    const bool builtProjectedScratch =
        backendRenderer &&
        renderRouting.buildProjectedScratch &&
        buildProjectedModelScratch(
            camera,
            surfaceWidth,
            surfaceHeight,
            visual,
            worldPos,
            yawDeg,
            side);
    const auto previewBodySummary =
        builtProjectedScratch
            ? game::runtime::shared_preview_body_presentation_path::inspectPreviewBodyPath(
                  modelScratch,
                  backendRenderer && backendRenderer->supportsWorldSceneFastPath())
            : game::runtime::shared_preview_body_presentation_path::PreviewBodyPathSummary{};
    PreviewBodyDebugState* debugState =
        (side == PokemonSide::Player) ? &lastAttackerBodyDebug : &lastTargetBodyDebug;
    if (debugState) {
        debugState->speciesName = visual.speciesName;
        debugState->builtProjectedScratch = builtProjectedScratch;
        debugState->haveDirectBodySample = haveDirectBodySample;
        debugState->pathSummary = previewBodySummary;
        if (side == PokemonSide::Player) {
            maybeTracePreviewBodyPath(*debugState, lastAttackerBodyTraceLine);
        } else {
            maybeTracePreviewBodyPath(*debugState, lastTargetBodyTraceLine);
        }
    }
    const bool canUseProjectedBody =
        renderRouting.allowProjectedBody &&
        (previewBodySummary.decision ==
             game::runtime::shared_preview_body_presentation_path::PreviewBodyPathDecision::
                 ProjectedWorldScene ||
         previewBodySummary.decision ==
             game::runtime::shared_preview_body_presentation_path::PreviewBodyPathDecision::
                 ProjectedIndexedScratch);
    const bool hasAuthoredFireBatches =
        builtProjectedScratch &&
        previewBodySummary.authoredFireBatchCount > 0u;
    const bool authoredFireAlreadySubmitted =
        hasAuthoredFireBatches &&
        canUseProjectedBody;
    if (canUseProjectedBody) {
        submitScratch(
            camera,
            surfaceWidth,
            surfaceHeight,
            modelScratch,
            false,
            true);
    } else if (haveDirectBodySample) {
        // Keep the direct model path as a safe fallback until every backend can
        // guarantee materially faithful projected body output in preview.
        preview_animated_model_presentation::drawDirectBody(camera, visual, bodySample);
    }

    if (game::runtime::shared_tail_fire_coordinator::speciesUsesTailFireMeshPlayback(
            visual.speciesName)) {
        if (hasAuthoredFireBatches && !authoredFireAlreadySubmitted) {
            auto& scratch = tailFireScratch;
            game::runtime::session_render_scratch::ensureCapacity(scratch);
            game::runtime::session_render_scratch::beginFrame(scratch, true, backendRenderer.get());
            scratch.worldIndexedBatches.reserve(modelScratch.worldIndexedBatches.size());
            for (const auto& batch : modelScratch.worldIndexedBatches) {
                if (!game::runtime::shared_tail_fire_playback_policy::batchUsesAuthoredFireMesh(batch)) {
                    continue;
                }
                scratch.worldIndexedBatches.push_back(batch);
            }
            submitScratch(
                camera,
                surfaceWidth,
                surfaceHeight,
                scratch,
                false,
                false);
        } else if (!authoredFireAlreadySubmitted) {
            renderTailFireBillboards(
                camera,
                surfaceWidth,
                surfaceHeight,
                visual,
                worldPos,
                yawDeg,
                side);
        }
    }
}

PokemonAutochessVfxPreviewProject::PokemonAutochessVfxPreviewProject()
    : board_(nullptr)
    , impl_(std::make_unique<Impl>()) {
    effects_.push_back(std::make_unique<GrowlPreviewEffect>());
    effects_.push_back(std::make_unique<TacklePreviewEffect>());
    effects_.push_back(std::make_unique<ScratchPreviewEffect>());
    effects_.push_back(std::make_unique<LeechSeedPreviewEffect>());
    effects_.push_back(
        std::make_unique<GameplayParticlePreviewEffect>(
            GameplayParticlePreviewEffect::Kind::
                AquaSwoosh));
    effects_.push_back(
        std::make_unique<GameplayParticlePreviewEffect>(
            GameplayParticlePreviewEffect::Kind::
                ClawSwipe));
    effects_.push_back(
        std::make_unique<GameplayParticlePreviewEffect>(
            GameplayParticlePreviewEffect::Kind::
                GrassImpact));
    effects_.push_back(
        std::make_unique<GameplayParticlePreviewEffect>(
            GameplayParticlePreviewEffect::Kind::
                HealPlus));
    effects_.push_back(
        std::make_unique<GameplayParticlePreviewEffect>(
            GameplayParticlePreviewEffect::Kind::
                LeechSeedDrain));
    effects_.push_back(
        std::make_unique<GameplayParticlePreviewEffect>(
            GameplayParticlePreviewEffect::Kind::
                TackleImpact));
    effects_.push_back(
        std::make_unique<GameplayParticlePreviewEffect>(
            GameplayParticlePreviewEffect::Kind::
                TailFire));
}

PokemonAutochessVfxPreviewProject::~PokemonAutochessVfxPreviewProject() = default;

std::string_view PokemonAutochessVfxPreviewProject::projectName() const {
    return "PokemonAutochess";
}

std::size_t PokemonAutochessVfxPreviewProject::effectCount() const {
    return effects_.size();
}

engine::tools::vfx_preview::IVfxPreviewEffect&
PokemonAutochessVfxPreviewProject::effectAt(std::size_t index) {
    if (index >= effects_.size()) throw std::out_of_range("Invalid preview effect index");
    return *effects_[index];
}

const engine::tools::vfx_preview::IVfxPreviewEffect&
PokemonAutochessVfxPreviewProject::effectAt(std::size_t index) const {
    if (index >= effects_.size()) throw std::out_of_range("Invalid preview effect index");
    return *effects_[index];
}

std::size_t PokemonAutochessVfxPreviewProject::rigCount() const {
    return 2u;
}

std::string_view PokemonAutochessVfxPreviewProject::rigName(std::size_t index) const {
    switch (static_cast<RigKind>(index)) {
    case RigKind::BoardLines: return "Lines";
    case RigKind::PokemonModels: return "3D Models";
    }
    return "Unknown";
}

bool PokemonAutochessVfxPreviewProject::defaultPrimaryBackdropEnabled(std::size_t rigIndex) const {
    (void)rigIndex;
    return true;
}

bool PokemonAutochessVfxPreviewProject::defaultSecondaryBackdropEnabled(std::size_t rigIndex) const {
    (void)rigIndex;
    return true;
}

void PokemonAutochessVfxPreviewProject::onEffectActivated(std::size_t effectIndex) {
    impl_->activeEffectIndex = effectIndex;
    impl_->pendingReplayAction = Impl::PendingReplayAction::None;
    impl_->pendingReplayTriggerAnimTimeSec = 0.0f;
    impl_->applyEffectPreviewSpecies(effectAt(effectIndex));
    impl_->attackerVisual.previewUseExactClipMotion =
        effectAt(effectIndex).wantsExactClipMotionPreview();
    impl_->targetVisual.previewUseExactClipMotion = false;
    impl_->attackerVisual.clearPreviewAnimation();
    impl_->targetVisual.clearPreviewAnimation();
    impl_->resetTailFirePreview();
}

void PokemonAutochessVfxPreviewProject::requestReplay(
    std::size_t effectIndex,
    engine::tools::vfx_preview::PreviewSceneState& scene) {
    impl_->activeEffectIndex = effectIndex;
    impl_->pendingReplayAction = Impl::PendingReplayAction::Replay;
    impl_->pendingReplayTriggerAnimTimeSec = 0.0f;
    impl_->applyEffectPreviewSpecies(effectAt(effectIndex));
    impl_->attackerVisual.clearPreviewAnimation();
    impl_->targetVisual.clearPreviewAnimation();

    if (static_cast<RigKind>(impl_->activeRigIndex) != RigKind::PokemonModels) {
        effectAt(effectIndex).replay(scene);
        impl_->pendingReplayAction = Impl::PendingReplayAction::None;
        return;
    }

    engine::tools::vfx_preview::IVfxPreviewEffect* activeEffect =
        effectIndex < effects_.size() ? effects_[effectIndex].get() : nullptr;
    impl_->attackerVisual.previewUseExactClipMotion =
        activeEffect && activeEffect->wantsExactClipMotionPreview();
    impl_->targetVisual.previewUseExactClipMotion = false;
    impl_->updateEffectImpactPoint(activeEffect, scene);
    if (impl_->attackerVisual.previewUseExactClipMotion) {
        scene.showOrientationGuide = false;
    }
    const auto casterAnimRequest =
        activeEffect ? activeEffect->casterAnimationRequest()
                     : engine::tools::vfx_preview::PreviewCasterAnimationRequest{};

    impl_->ensurePokemonConfigLoaded();
    impl_->ensureAttackAnimConfigLoaded();
    impl_->attackerVisual.ensureLoaded(impl_->pokemonConfig);

    if (!casterAnimRequest.valid() || !impl_->attackerVisual.valid) {
        effectAt(effectIndex).replay(scene);
        impl_->pendingReplayAction = Impl::PendingReplayAction::None;
        return;
    }

    const std::string clipName = impl_->attackAnimConfig.getClipName(
        impl_->attackerVisual.speciesName,
        std::string(casterAnimRequest.kind),
        std::string(casterAnimRequest.move),
        std::string(casterAnimRequest.phase));
    const int clipIndex = impl_->attackerVisual.resolveClipAnimIndex(clipName);
    if (clipIndex < 0) {
        effectAt(effectIndex).replay(scene);
        impl_->pendingReplayAction = Impl::PendingReplayAction::None;
        return;
    }

    const float clipDur = impl_->attackerVisual.animationDurationSec(clipIndex);
    const float windowSec =
        impl_->computeReplayWindowSec(impl_->attackerVisual, casterAnimRequest);
    const bool exactClipMotionPreview =
        activeEffect && activeEffect->wantsExactClipMotionPreview();
    const float attackAnimSpeed =
        exactClipMotionPreview
            ? 1.0f
            : ((windowSec > 0.0f && clipDur > 0.0f) ? (clipDur / windowSec) : 1.0f);
    const int hitFrame = impl_->attackAnimConfig.getHitFrame(
        impl_->attackerVisual.speciesName,
        std::string(casterAnimRequest.kind),
        std::string(casterAnimRequest.move));
    float triggerAnimTimeSec = 0.0f;
    if (hitFrame > 0) {
        triggerAnimTimeSec =
            std::max(0.0f, static_cast<float>(hitFrame) / impl_->attackerVisual.animationFps());
        if (clipDur > 0.0f) {
            triggerAnimTimeSec = std::min(triggerAnimTimeSec, std::max(0.0f, clipDur - 0.0001f));
        }
    }

    impl_->attackerVisual.setPreviewAnimation(clipIndex, false, true, 0.0f, attackAnimSpeed);
    impl_->attackerVisual.previewAttackMoveName = std::string(casterAnimRequest.move);
    impl_->pendingReplayTriggerAnimTimeSec = triggerAnimTimeSec;
    if (!(impl_->pendingReplayTriggerAnimTimeSec > 0.0001f)) {
        effectAt(effectIndex).replay(scene);
        impl_->pendingReplayAction = Impl::PendingReplayAction::None;
    }
}

void PokemonAutochessVfxPreviewProject::requestReload(
    std::size_t effectIndex,
    engine::tools::vfx_preview::PreviewSceneState& scene) {
    impl_->activeEffectIndex = effectIndex;
    impl_->pendingReplayAction = Impl::PendingReplayAction::Reload;
    impl_->pendingReplayTriggerAnimTimeSec = 0.0f;
    impl_->applyEffectPreviewSpecies(effectAt(effectIndex));
    impl_->attackerVisual.clearPreviewAnimation();
    impl_->targetVisual.clearPreviewAnimation();

    if (static_cast<RigKind>(impl_->activeRigIndex) != RigKind::PokemonModels) {
        effectAt(effectIndex).reload(scene);
        impl_->pendingReplayAction = Impl::PendingReplayAction::None;
        return;
    }

    engine::tools::vfx_preview::IVfxPreviewEffect* activeEffect =
        effectIndex < effects_.size() ? effects_[effectIndex].get() : nullptr;
    impl_->attackerVisual.previewUseExactClipMotion =
        activeEffect && activeEffect->wantsExactClipMotionPreview();
    impl_->targetVisual.previewUseExactClipMotion = false;
    impl_->updateEffectImpactPoint(activeEffect, scene);
    if (impl_->attackerVisual.previewUseExactClipMotion) {
        scene.showOrientationGuide = false;
    }
    const auto casterAnimRequest =
        activeEffect ? activeEffect->casterAnimationRequest()
                     : engine::tools::vfx_preview::PreviewCasterAnimationRequest{};

    impl_->ensurePokemonConfigLoaded();
    impl_->ensureAttackAnimConfigLoaded();
    impl_->attackerVisual.ensureLoaded(impl_->pokemonConfig);

    if (!casterAnimRequest.valid() || !impl_->attackerVisual.valid) {
        effectAt(effectIndex).reload(scene);
        impl_->pendingReplayAction = Impl::PendingReplayAction::None;
        return;
    }

    const std::string clipName = impl_->attackAnimConfig.getClipName(
        impl_->attackerVisual.speciesName,
        std::string(casterAnimRequest.kind),
        std::string(casterAnimRequest.move),
        std::string(casterAnimRequest.phase));
    const int clipIndex = impl_->attackerVisual.resolveClipAnimIndex(clipName);
    if (clipIndex < 0) {
        effectAt(effectIndex).reload(scene);
        impl_->pendingReplayAction = Impl::PendingReplayAction::None;
        return;
    }

    const float clipDur = impl_->attackerVisual.animationDurationSec(clipIndex);
    const float windowSec =
        impl_->computeReplayWindowSec(impl_->attackerVisual, casterAnimRequest);
    const bool exactClipMotionPreview =
        activeEffect && activeEffect->wantsExactClipMotionPreview();
    const float attackAnimSpeed =
        exactClipMotionPreview
            ? 1.0f
            : ((windowSec > 0.0f && clipDur > 0.0f) ? (clipDur / windowSec) : 1.0f);
    const int hitFrame = impl_->attackAnimConfig.getHitFrame(
        impl_->attackerVisual.speciesName,
        std::string(casterAnimRequest.kind),
        std::string(casterAnimRequest.move));
    float triggerAnimTimeSec = 0.0f;
    if (hitFrame > 0) {
        triggerAnimTimeSec =
            std::max(0.0f, static_cast<float>(hitFrame) / impl_->attackerVisual.animationFps());
        if (clipDur > 0.0f) {
            triggerAnimTimeSec = std::min(triggerAnimTimeSec, std::max(0.0f, clipDur - 0.0001f));
        }
    }

    impl_->attackerVisual.setPreviewAnimation(clipIndex, false, true, 0.0f, attackAnimSpeed);
    impl_->attackerVisual.previewAttackMoveName = std::string(casterAnimRequest.move);
    impl_->pendingReplayTriggerAnimTimeSec = triggerAnimTimeSec;
    if (!(impl_->pendingReplayTriggerAnimTimeSec > 0.0001f)) {
        effectAt(effectIndex).reload(scene);
        impl_->pendingReplayAction = Impl::PendingReplayAction::None;
    }
}

bool PokemonAutochessVfxPreviewProject::isReplayPending(std::size_t effectIndex) const {
    return effectIndex == impl_->activeEffectIndex &&
           impl_->pendingReplayAction != Impl::PendingReplayAction::None;
}

bool PokemonAutochessVfxPreviewProject::allowAutoReplay(std::size_t effectIndex,
                                                        std::size_t rigIndex) const {
    if (effectIndex != impl_->activeEffectIndex) return true;
    if (static_cast<RigKind>(rigIndex) != RigKind::PokemonModels) return true;
    return !impl_->attackerVisual.previewAnimationActive();
}

void PokemonAutochessVfxPreviewProject::applyRigDefaults(
    std::size_t rigIndex,
    engine::tools::vfx_preview::PreviewSceneState& scene) const {
    impl_->activeRigIndex = rigIndex;
    const float cellSize = impl_->boardCellSize();
    switch (static_cast<RigKind>(rigIndex)) {
    case RigKind::BoardLines:
        scene.emitter = previewBoardCellToWorld(3, 3, 0.42f, cellSize);
        scene.target = previewBoardCellToWorld(3, 4, 0.35f, cellSize);
        break;
    case RigKind::PokemonModels:
        scene.emitter = previewBoardCellToWorld(3, 3, 0.62f, cellSize);
        scene.target = previewBoardCellToWorld(3, 4, 0.35f, cellSize);
        break;
    }
}

void PokemonAutochessVfxPreviewProject::constrainScene(
    std::size_t rigIndex,
    engine::tools::vfx_preview::PreviewSceneState& scene) const {
    const RigKind rig = static_cast<RigKind>(rigIndex);
    if (rig != RigKind::BoardLines && rig != RigKind::PokemonModels) return;

    const float cellSize = impl_->boardCellSize();
    const glm::ivec2 emitterCell = previewWorldToBoardCell(scene.emitter, cellSize);
    const glm::ivec2 targetCellGuess = previewWorldToBoardCell(scene.target, cellSize);
    glm::ivec2 delta = targetCellGuess - emitterCell;
    if (delta == glm::ivec2(0)) {
        delta = glm::ivec2(0, 1);
    } else if (std::abs(delta.x) > std::abs(delta.y)) {
        delta = glm::ivec2(delta.x >= 0 ? 1 : -1, 0);
    } else {
        delta = glm::ivec2(0, delta.y >= 0 ? 1 : -1);
    }

    const glm::ivec2 targetCell = previewAdjacentCell(emitterCell, delta);
    scene.emitter = previewBoardCellToWorld(emitterCell.x, emitterCell.y, scene.emitter.y, cellSize);
    scene.target = previewBoardCellToWorld(targetCell.x, targetCell.y, 0.35f, cellSize);
}

void PokemonAutochessVfxPreviewProject::update(
    float dt,
    std::size_t rigIndex,
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    if (static_cast<RigKind>(rigIndex) != RigKind::PokemonModels) {
        impl_->pendingReplayAction = Impl::PendingReplayAction::None;
        impl_->pendingReplayTriggerAnimTimeSec = 0.0f;
        impl_->attackerVisual.clearPreviewAnimation();
        impl_->targetVisual.clearPreviewAnimation();
        impl_->resetTailFirePreview();
        return;
    }

    impl_->ensurePokemonConfigLoaded();
    impl_->ensureAttackAnimConfigLoaded();
    impl_->attackerVisual.ensureLoaded(impl_->pokemonConfig);
    impl_->targetVisual.ensureLoaded(impl_->pokemonConfig);

    impl_->targetVisual.clearPreviewAnimation();

    const float prevAttackAnimTimeSec =
        impl_->attackerVisual.previewAnimationActive() ? impl_->attackerVisual.currentAnimTimeSec() : 0.0f;
    impl_->attackerVisual.update(dt);
    impl_->targetVisual.update(dt);

    if (impl_->pendingReplayAction != Impl::PendingReplayAction::None &&
        impl_->attackerVisual.previewAnimationActive()) {
        const float currentAttackAnimTimeSec = impl_->attackerVisual.currentAnimTimeSec();
        if (currentAttackAnimTimeSec >= impl_->pendingReplayTriggerAnimTimeSec &&
            prevAttackAnimTimeSec < impl_->pendingReplayTriggerAnimTimeSec + 0.0001f) {
            if (impl_->pendingReplayAction == Impl::PendingReplayAction::Reload) {
                effectAt(impl_->activeEffectIndex).reload(scene);
            } else {
                effectAt(impl_->activeEffectIndex).replay(scene);
            }
            impl_->pendingReplayAction = Impl::PendingReplayAction::None;
            impl_->pendingReplayTriggerAnimTimeSec = 0.0f;
        }
    }
    impl_->tailFireSimNowSec += static_cast<double>(std::max(0.0f, dt));
    if (impl_->attackerVisual.previewAnimFinished) {
        impl_->attackerVisual.clearPreviewAnimation();
    }
}

void PokemonAutochessVfxPreviewProject::renderBackdrop(
    const engine::tools::vfx_preview::PreviewFrameContext& frame,
    std::size_t rigIndex,
    const engine::tools::vfx_preview::PreviewSceneState& scene,
    bool primaryBackdropEnabled,
    bool secondaryBackdropEnabled) {
    impl_->syncBackendSurface(frame.surfaceWidth, frame.surfaceHeight);

    if (!board_) {
        board_ = std::make_unique<BoardRenderer>(
            kPreviewBoardRows,
            kPreviewBoardCols,
            impl_->boardCellSize(),
            nullptr);
    }

    if (secondaryBackdropEnabled) {
        impl_->appendBackdropTiles(frame.camera, frame.surfaceWidth, frame.surfaceHeight);
        impl_->submitScratch(
            frame.camera,
            frame.surfaceWidth,
            frame.surfaceHeight,
            impl_->backdropScratch,
            !primaryBackdropEnabled);
    }

    if (primaryBackdropEnabled && board_) {
        board_->draw(frame.camera);
    }

    if (static_cast<RigKind>(rigIndex) != RigKind::PokemonModels) return;

    impl_->ensurePokemonConfigLoaded();
    impl_->attackerVisual.ensureLoaded(impl_->pokemonConfig);
    impl_->targetVisual.ensureLoaded(impl_->pokemonConfig);

    const glm::vec3 casterPos(scene.emitter.x, 0.0f, scene.emitter.z);
    const glm::vec3 targetPos(scene.target.x, 0.0f, scene.target.z);

    const float attackerYaw = computeYawDegreesFromForward(targetPos - casterPos);
    const float targetYaw = computeYawDegreesFromForward(casterPos - targetPos);

    impl_->renderPreviewUnit(
        frame.camera,
        frame.surfaceWidth,
        frame.surfaceHeight,
        impl_->attackerVisual,
        casterPos,
        attackerYaw,
        PokemonSide::Player);
    impl_->renderPreviewUnit(
        frame.camera,
        frame.surfaceWidth,
        frame.surfaceHeight,
        impl_->targetVisual,
        targetPos,
        targetYaw,
        PokemonSide::Enemy);
}

void PokemonAutochessVfxPreviewProject::appendDebugMarkers(
    engine::tools::vfx_preview::IPreviewDebugDraw& draw,
    const engine::tools::vfx_preview::PreviewSceneState& scene) const {
    const glm::vec3 emitterColor(1.0f, 0.52f, 0.16f);
    const glm::vec3 targetColor(0.28f, 0.95f, 0.55f);
    const glm::vec3 guideColor(0.95f, 0.90f, 0.35f);
    const glm::vec3 debugTarget =
        scene.useCustomImpactPoint ? scene.impactPoint : scene.target;

    if (scene.showEmitterMarker) {
        draw.addCross(scene.emitter, 0.16f, emitterColor);
        draw.addCircleXZ(glm::vec3(scene.emitter.x, 0.015f, scene.emitter.z), 0.20f, emitterColor, 28);
    }

    if (scene.showTargetMarker) {
        draw.addCross(debugTarget, 0.18f, targetColor);
        draw.addCircleXZ(glm::vec3(debugTarget.x, 0.015f, debugTarget.z), 0.24f, targetColor, 28);
    }

    if (scene.showOrientationGuide) {
        const glm::vec3 guideStart = scene.emitter + glm::vec3(0.0f, 0.02f, 0.0f);
        const glm::vec3 guideEnd = glm::vec3(debugTarget.x, guideStart.y, debugTarget.z);
        draw.addArrow(guideStart, guideEnd, guideColor);
    }
}

std::vector<std::string> PokemonAutochessVfxPreviewProject::overlayLines(
    const engine::tools::vfx_preview::PreviewSceneState& scene,
    std::size_t rigIndex) const {
    (void)scene;
    switch (static_cast<RigKind>(rigIndex)) {
    case RigKind::BoardLines:
        return {
            "Lines mode uses the real board cell spacing and keeps the target one cell away.",
            "Use this to judge timing and staging without the model layer."
        };
    case RigKind::PokemonModels:
        return {
            "3D Models mode uses the same board spacing, gameplay clip timing, and shared Tail Fire playback policy as the game.",
            "The preview uses projected body presentation when the backend can submit the same material-faithful body output; otherwise it falls back to the direct model draw and keeps Tail Fire on the shared gameplay path.",
            "Body path attacker=" + impl_->lastAttackerBodyDebug.speciesName + ":" +
                previewBodyPathLabel(impl_->lastAttackerBodyDebug.pathSummary.decision) +
                " target=" + impl_->lastTargetBodyDebug.speciesName + ":" +
                previewBodyPathLabel(impl_->lastTargetBodyDebug.pathSummary.decision),
            "Scratch attacker(ws=" +
                std::to_string(impl_->lastAttackerBodyDebug.pathSummary.worldSceneDrawClassCount) +
                ", idx=" +
                std::to_string(impl_->lastAttackerBodyDebug.pathSummary.worldIndexedBatchCount) +
                ", litBody=" +
                std::to_string(impl_->lastAttackerBodyDebug.pathSummary.litTexturedIndexedBodyBatchCount) +
                ", fire=" +
                std::to_string(impl_->lastAttackerBodyDebug.pathSummary.authoredFireBatchCount) +
                ")" +
                " target(ws=" +
                std::to_string(impl_->lastTargetBodyDebug.pathSummary.worldSceneDrawClassCount) +
                ", idx=" +
                std::to_string(impl_->lastTargetBodyDebug.pathSummary.worldIndexedBatchCount) +
                ", litBody=" +
                std::to_string(impl_->lastTargetBodyDebug.pathSummary.litTexturedIndexedBodyBatchCount) +
                ", fire=" +
                std::to_string(impl_->lastTargetBodyDebug.pathSummary.authoredFireBatchCount) +
                ")",
            "Indexed-body path=" +
                std::string(
                    game::runtime::shared_preview_body_presentation_path::
                            indexedScratchPathAllowedForPreview()
                        ? "enabled"
                        : "disabled")
        };
    }
    return {};
}

} // namespace game::preview

