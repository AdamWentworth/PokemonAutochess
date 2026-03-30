#include "game/preview/PokemonAutochessVfxPreviewProject.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/core/Paths.h"
#include "engine/render/BoardRenderer.h"
#include "engine/render/Camera3D.h"
#include "engine/render/Model.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "game/GameConfig.h"
#include "game/PokemonInstance.h"
#include "game/config/AttackAnimConfigLoader.h"
#include "game/config/GameDataDb.h"
#include "game/config/AnimSetLoader.h"
#include "game/config/PokemonConfigLoader.h"
#include "game/preview/effects/GrowlPreviewEffect.h"
#include "game/preview/effects/LeechSeedPreviewEffect.h"
#include "game/runtime/render_prep/UnitVisuals.h"
#include "game/runtime/render_prep/ProceduralPose.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/session/SessionRenderScratch.h"
#include "game/runtime/session/SessionTextureCache.h"
#include "game/runtime/session/SessionWorldBackdrop.h"
#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/projected/SharedProjectedUnitModelRenderer.h"
#include "game/runtime/shared/projected/SharedProjectedWorldSceneHelpers.h"
#include "game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireFallbackEmitter.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "game/vfx/TailFireVFXConfigDB.h"
#include "game/world/MoveImpactMath.h"

namespace game::preview {

namespace {

constexpr int kPreviewBoardCols = 8;
constexpr int kPreviewBoardRows = 8;
constexpr int kPreviewBenchSlots = 8;

struct PreviewCombatTuning {
    float chargedCdMult = 2.0f;
    float minChargedRequestSec = 0.85f;
    float attackSpeedScale = 1.0f;
    float speedBaseline = 1.0f;
    float speedMin = 0.35f;
    float speedMax = 3.0f;
};

bool assignLuaFloatOverride(const std::string& text, const char* key, float& outValue) {
    if (!key || !key[0]) return false;
    const std::string needle = std::string(key) + " =";
    const std::size_t pos = text.find(needle);
    if (pos == std::string::npos) return false;
    std::size_t cursor = pos + needle.size();
    while (cursor < text.size() &&
           std::isspace(static_cast<unsigned char>(text[cursor]))) {
        ++cursor;
    }
    std::size_t end = cursor;
    while (end < text.size()) {
        const char c = text[end];
        if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+')) {
            break;
        }
        ++end;
    }
    if (end <= cursor) return false;
    try {
        outValue = std::stof(text.substr(cursor, end - cursor));
        return true;
    } catch (...) {
        return false;
    }
}

PreviewCombatTuning loadPreviewCombatTuningFromConfig() {
    PreviewCombatTuning tuning;
    const std::string path = engine::paths::data("scripts/config/combat_tuning.lua");
    std::ifstream in(path);
    if (!in.is_open()) return tuning;

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    (void)assignLuaFloatOverride(text, "CHARGED_CD_MULT", tuning.chargedCdMult);
    (void)assignLuaFloatOverride(text, "MIN_CHARGED_REQUEST_SEC", tuning.minChargedRequestSec);
    (void)assignLuaFloatOverride(text, "ATTACK_SPEED_SCALE", tuning.attackSpeedScale);
    (void)assignLuaFloatOverride(text, "SPEED_BASELINE", tuning.speedBaseline);
    (void)assignLuaFloatOverride(text, "SPEED_MIN", tuning.speedMin);
    (void)assignLuaFloatOverride(text, "SPEED_MAX", tuning.speedMax);
    return tuning;
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(),
                   value.end(),
                   value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

float resolvePreviewModelScaleCorrection(const std::shared_ptr<Model>& model,
                                         const std::string& scaleModeRaw,
                                         const std::string& axisModeRaw) {
    if (!model) return 1.0f;

    const std::string scaleMode = lowerCopy(scaleModeRaw);
    if (scaleMode.empty() || scaleMode == "native" || scaleMode == "raw") {
        const float importerScale = std::max(0.0f, model->getScaleFactor());
        if (importerScale <= 1e-6f) return 1.0f;
        return 1.0f / importerScale;
    }

    if (scaleMode != "normalized") {
        const float importerScale = std::max(0.0f, model->getScaleFactor());
        if (importerScale <= 1e-6f) return 1.0f;
        return 1.0f / importerScale;
    }

    if (!model->hasBounds()) return 1.0f;

    const std::string axisMode = lowerCopy(axisModeRaw);
    if (axisMode.empty() || axisMode == "max") return 1.0f;

    const glm::vec3 ext = model->getBoundsMax() - model->getBoundsMin();
    const float ex = std::max(0.0f, ext.x);
    const float ey = std::max(0.0f, ext.y);
    const float ez = std::max(0.0f, ext.z);
    const float maxExtent = std::max(ex, std::max(ey, ez));
    if (maxExtent <= 1e-6f) return 1.0f;

    float chosenExtent = maxExtent;
    if (axisMode == "x") chosenExtent = ex;
    else if (axisMode == "y") chosenExtent = ey;
    else if (axisMode == "z") chosenExtent = ez;
    else if (axisMode == "median") {
        std::array<float, 3> arr{ex, ey, ez};
        std::sort(arr.begin(), arr.end());
        chosenExtent = arr[1];
    }

    if (chosenExtent <= 1e-6f) return 1.0f;
    return maxExtent / chosenExtent;
}

float computeYawDegreesFromForward(const glm::vec3& forward) {
    glm::vec3 safe(forward.x, 0.0f, forward.z);
    const float lenSq = glm::dot(safe, safe);
    if (lenSq <= 0.000001f) {
        safe = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        safe /= std::sqrt(lenSq);
    }
    return glm::degrees(std::atan2(safe.x, safe.z));
}

glm::mat4 makePreviewInstanceTransform(const glm::vec3& pos, float yawDeg, float scale) {
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), pos);
    glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), glm::radians(yawDeg), glm::vec3(0, 1, 0));
    glm::mat4 scaling = glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    return translation * rotationY * scaling;
}

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

float previewBoardOriginX(float cellSize) {
    return -((static_cast<float>(kPreviewBoardCols) * cellSize) / 2.0f) +
           cellSize * 0.5f;
}

float previewBoardOriginZ(float cellSize) {
    return -((static_cast<float>(kPreviewBoardRows) * cellSize) / 2.0f) +
           cellSize * 0.5f;
}

glm::ivec2 clampBoardCell(const glm::ivec2& cell) {
    return glm::ivec2(
        std::clamp(cell.x, 0, kPreviewBoardCols - 1),
        std::clamp(cell.y, 0, kPreviewBoardRows - 1));
}

glm::ivec2 previewWorldToBoardCell(const glm::vec3& pos, float cellSize) {
    const int col = static_cast<int>(std::round((pos.x - previewBoardOriginX(cellSize)) / cellSize));
    const int row = static_cast<int>(std::round((pos.z - previewBoardOriginZ(cellSize)) / cellSize));
    return clampBoardCell(glm::ivec2(col, row));
}

glm::vec3 previewBoardCellToWorld(int col, int row, float y, float cellSize) {
    const glm::ivec2 clamped = clampBoardCell(glm::ivec2(col, row));
    return glm::vec3(
        previewBoardOriginX(cellSize) + static_cast<float>(clamped.x) * cellSize,
        y,
        previewBoardOriginZ(cellSize) + static_cast<float>(clamped.y) * cellSize);
}

glm::ivec2 previewAdjacentCell(const glm::ivec2& origin, glm::ivec2 dir) {
    if (dir == glm::ivec2(0)) dir = glm::ivec2(0, 1);
    glm::ivec2 candidate = origin + dir;
    if (candidate.x >= 0 && candidate.x < kPreviewBoardCols &&
        candidate.y >= 0 && candidate.y < kPreviewBoardRows) {
        return candidate;
    }

    candidate = origin - dir;
    if (candidate.x >= 0 && candidate.x < kPreviewBoardCols &&
        candidate.y >= 0 && candidate.y < kPreviewBoardRows) {
        return candidate;
    }

    static constexpr glm::ivec2 kFallbackDirs[] = {
        {0, 1}, {1, 0}, {0, -1}, {-1, 0}
    };
    for (const glm::ivec2 fallback : kFallbackDirs) {
        candidate = origin + fallback;
        if (candidate.x >= 0 && candidate.x < kPreviewBoardCols &&
            candidate.y >= 0 && candidate.y < kPreviewBoardRows) {
            return candidate;
        }
    }

    return origin;
}

struct PreviewPokemonVisual {
    std::string speciesName;
    std::shared_ptr<Model> model;
    std::string loadError;
    std::string modelPath;
    float finalScale = 1.0f;
    float animTimeSec = 0.0f;
    int idleAnimIndex = -1;
    int previewAnimIndex = -1;
    float previewAnimTimeSec = 0.0f;
    float previewAnimPlaybackSpeed = 1.0f;
    bool previewAnimLoop = false;
    bool previewAnimFinished = false;
    bool attemptedLoad = false;
    bool valid = false;
    PokemonInstance runtimeLikeUnit;
    PokemonStats stats{};

    void ensureLoaded(PokemonConfigLoader& pokemonConfig) {
        if (attemptedLoad) return;
        attemptedLoad = true;

        const PokemonStats* speciesStats = pokemonConfig.getStats(speciesName);
        if (!speciesStats) {
            loadError = "No Pokemon config entry for '" + speciesName + "'";
            return;
        }
        this->stats = *speciesStats;

        modelPath = engine::paths::asset(std::string("models/") + speciesStats->model);

        try {
            model = std::make_shared<Model>(modelPath);
        } catch (const std::exception& ex) {
            loadError = ex.what();
            model.reset();
            return;
        }

        runtimeLikeUnit = PokemonInstance{};
        runtimeLikeUnit.name = speciesName;
        runtimeLikeUnit.id = PokemonInstance::getNextUnitID();
        runtimeLikeUnit.model = model;
        runtimeLikeUnit.backendModelPath = modelPath;
        runtimeLikeUnit.animIndexCacheSourceModelPath = modelPath;
        runtimeLikeUnit.backendAnimDurationsSourceModelPath = modelPath;
        runtimeLikeUnit.modelScaleCorrection =
            resolvePreviewModelScaleCorrection(model, speciesStats->modelScaleMode, speciesStats->modelScaleAxis);
        runtimeLikeUnit.speciesScale = std::max(0.05f, speciesStats->visualScale);
        runtimeLikeUnit.movementSpeed = std::max(0.01f, speciesStats->movementSpeed);
        runtimeLikeUnit.visualScale = 1.0f;
        runtimeLikeUnit.captureScale = 1.0f;
        AnimSet::applyAnimSetOverrides(runtimeLikeUnit, modelPath, nullptr);

        idleAnimIndex = runtimeLikeUnit.animIdleIndex;
        if (idleAnimIndex < 0 && model->getAnimationCount() > 0) {
            idleAnimIndex = 0;
        }
        finalScale = std::max(0.05f, computeModelWorldScaleForMoveImpact(runtimeLikeUnit));
        valid = true;
    }

    int resolveClipAnimIndex(const std::string& clipName) const {
        if (!model || clipName.empty()) return -1;
        return AnimSet::resolveAnimIndex(model.get(), clipName);
    }

    float animationDurationSec(int animIndex) const {
        if (!model || animIndex < 0) return 0.0f;
        return model->getAnimationDurationSec(animIndex);
    }

    float animationFps() const {
        return (runtimeLikeUnit.animFps > 0.0f) ? runtimeLikeUnit.animFps : 24.0f;
    }

    bool previewAnimationActive() const {
        return previewAnimIndex >= 0;
    }

    int currentAnimIndex() const {
        return previewAnimIndex >= 0 ? previewAnimIndex : idleAnimIndex;
    }

    float currentAnimTimeSec() const {
        return previewAnimIndex >= 0 ? previewAnimTimeSec : animTimeSec;
    }

    void setPreviewAnimation(int animIndex,
                             bool loop,
                             bool restart,
                             float startTimeSec = 0.0f,
                             float playbackSpeed = 1.0f) {
        if (animIndex < 0) {
            clearPreviewAnimation();
            return;
        }
        if (restart || previewAnimIndex != animIndex || previewAnimLoop != loop) {
            previewAnimTimeSec = std::max(0.0f, startTimeSec);
        }
        previewAnimIndex = animIndex;
        previewAnimPlaybackSpeed = std::max(0.0f, playbackSpeed);
        previewAnimLoop = loop;
        previewAnimFinished = false;
    }

    void clearPreviewAnimation() {
        previewAnimIndex = -1;
        previewAnimTimeSec = 0.0f;
        previewAnimPlaybackSpeed = 1.0f;
        previewAnimLoop = false;
        previewAnimFinished = false;
    }

    void update(float dt) {
        if (!valid || !model) return;

        const int animIndex = previewAnimIndex >= 0 ? previewAnimIndex : idleAnimIndex;
        if (animIndex < 0) return;
        const float dur = model->getAnimationDurationSec(animIndex);
        if (dur <= 0.0001f) return;

        if (previewAnimIndex >= 0) {
            previewAnimTimeSec = std::max(
                0.0f,
                previewAnimTimeSec + std::max(0.0f, dt) * std::max(0.0f, previewAnimPlaybackSpeed));
            if (previewAnimLoop) {
                previewAnimTimeSec = std::fmod(previewAnimTimeSec, dur);
                if (previewAnimTimeSec < 0.0f) previewAnimTimeSec += dur;
            } else {
                const float endTime = std::max(0.0f, dur - 0.0001f);
                previewAnimTimeSec = std::min(previewAnimTimeSec, endTime);
                previewAnimFinished = previewAnimTimeSec >= endTime;
            }
            return;
        }

        animTimeSec = std::fmod(animTimeSec + std::max(0.0f, dt), dur);
        if (animTimeSec < 0.0f) animTimeSec += dur;
    }

    void draw(const Camera3D& camera, const glm::vec3& worldPos, float yawDeg) const {
        if (!valid || !model) return;
        const int animIndex = currentAnimIndex();
        const float sampleTime = currentAnimTimeSec();
        if (animIndex < 0) return;
        model->drawAnimated(
            camera,
            makePreviewInstanceTransform(worldPos, yawDeg, finalScale),
            sampleTime,
            animIndex);
    }
};

PokemonInstance makePreviewRuntimeUnit(const PreviewPokemonVisual& visual,
                                       const glm::vec3& worldPos,
                                       float yawDeg,
                                       PokemonSide side) {
    PokemonInstance unit = visual.runtimeLikeUnit;
    unit.side = side;
    unit.alive = true;
    unit.fainting = false;
    unit.captureInProgress = false;
    unit.position = worldPos;
    unit.rotation = glm::vec3(0.0f, yawDeg, 0.0f);
    unit.visualYOffset = 0.0f;
    unit.activeAnimIndex = visual.currentAnimIndex();
    unit.currentAttackAnimIndex = visual.previewAnimationActive() ? visual.currentAnimIndex() : -1;
    unit.animTimeSec = visual.currentAnimTimeSec();
    unit.visualScale = 1.0f;
    unit.captureScale = 1.0f;
    return unit;
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
    std::unique_ptr<TailFireVFX> previewTailFire;
    TailFireVFX::Config tailFireConfig{};
    bool tailFireConfigLoaded = false;
    bool previewTailFireConfigured = false;
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

    void submitScratch(const Camera3D& camera,
                       int surfaceWidth,
                       int surfaceHeight,
                       game::runtime::session_render_scratch::RenderScratch& scratch,
                       bool renderProjectedLines,
                       bool indexedOnlyWhenAvailable = false);
    void appendBackdropTiles(const Camera3D& camera,
                             int surfaceWidth,
                             int surfaceHeight);
    bool appendModelBatches(const Camera3D& camera,
                            int surfaceWidth,
                            int surfaceHeight,
                            const PreviewPokemonVisual& visual,
                            const glm::vec3& worldPos,
                            float yawDeg,
                            PokemonSide side);
    void renderTailFirePreview(const Camera3D& camera) {
        if (!previewTailFireConfigured || !previewTailFire) return;
        previewTailFire->render(camera);
    }
    void renderTailFireBillboards(const Camera3D& camera,
                                  int surfaceWidth,
                                  int surfaceHeight,
                                  const PreviewPokemonVisual& visual,
                                  const glm::vec3& worldPos,
                                  float yawDeg,
                                  PokemonSide side) {
        if (!backendRenderer || !visual.valid) return;
        // Populate the exact projected-model state the runtime uses, but do not
        // submit the generated body geometry. For Charmander, the real game path
        // carries tail fire on authored backend mesh submeshes instead of the
        // synthetic fallback particle emitter.
        (void)appendModelBatches(camera, surfaceWidth, surfaceHeight, visual, worldPos, yawDeg, side);

        const auto isAuthoredTailFireBatch =
            [](const game::runtime::shared_world_batches::WorldIndexedBatch& batch) {
                const auto& resolved =
                    game::runtime::shared_world_batches::resolvedMaterialBatch(batch);
                const int fireFlags =
                    static_cast<int>(std::lround(resolved.materialFlags));
                return (fireFlags & 8) != 0;
            };

        if (std::any_of(modelScratch.worldIndexedBatches.begin(),
                        modelScratch.worldIndexedBatches.end(),
                        isAuthoredTailFireBatch)) {
            auto& scratch = tailFireScratch;
            game::runtime::session_render_scratch::ensureCapacity(scratch);
            game::runtime::session_render_scratch::beginFrame(
                scratch,
                true,
                backendRenderer.get());
            scratch.worldIndexedBatches.reserve(modelScratch.worldIndexedBatches.size());
            for (const auto& batch : modelScratch.worldIndexedBatches) {
                if (!isAuthoredTailFireBatch(batch)) continue;
                scratch.worldIndexedBatches.push_back(batch);
            }
            submitScratch(camera, surfaceWidth, surfaceHeight, scratch, false);
            return;
        }

        ensureTailFireConfigured();
        if (!tailFireConfigLoaded) return;

        auto& scratch = tailFireScratch;
        game::runtime::session_render_scratch::ensureCapacity(scratch);
        game::runtime::session_render_scratch::beginFrame(scratch, true, backendRenderer.get());

        std::vector<PokemonInstance> boardUnits;
        boardUnits.reserve(1u);
        boardUnits.push_back(makePreviewRuntimeUnit(visual, worldPos, yawDeg, side));
        static const std::vector<PokemonInstance> kNoBenchUnits;

        const glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
        const glm::mat4 invViewProj = glm::inverse(viewProj);
        (void)game::runtime::shared_tail_fire_fallback::appendSyntheticTailFire(
            {
                .worldCellSize = boardCellSize(),
                .simNowSec = tailFireSimNowSec,
                .cfg = &tailFireConfig,
                .anchors = &modelScratch.sharedTailFireAnchors,
                .pokemons = &boardUnits,
                .benchPokemons = &kNoBenchUnits,
                .appendSnapshot =
                    [&](const char* label, const ParticleSystem::RenderSnapshot& snapshot) -> bool {
                        return game::runtime::shared_particle_snapshot_billboards::appendSnapshotAsBillboards(
                            label,
                            snapshot,
                            viewProj,
                            invViewProj,
                            camera.getPosition(),
                            surfaceWidth,
                            surfaceHeight,
                            backendTextureByPath,
                            [&](const std::string& texturePath, bool flipVertical)
                                -> game::runtime::SharedBackendTextureCacheEntry* {
                                return ensureBackendTextureLoaded(texturePath, flipVertical);
                            },
                            &modelScratch.sharedTailFireAnchors,
                            false,
                            scratch.worldIndexedBatches);
                    },
            });

        submitScratch(camera, surfaceWidth, surfaceHeight, scratch, false);
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

    void ensureTailFireConfigured() {
        if (!tailFireConfigLoaded) {
            TailFireVFX::Config configData;
            TailFireVFXConfigDB::get().ensureLoaded();
            TailFireVFXConfigDB::get().applyIfAny("charmander", configData);
            configData.useUnitScaleChain = true;
            tailFireConfig = configData;
            tailFireConfigLoaded = true;
        }
        if (!previewTailFireConfigured) {
            previewTailFire = std::make_unique<TailFireVFX>();
            previewTailFire->setNameFilterCaseInsensitive("charmander");
            previewTailFire->setConfig(tailFireConfig);
            previewTailFireConfigured = true;
        }
    }
    void resetTailFirePreview() {
        tailFireSimNowSec = 0.0;
        previewTailFire.reset();
        previewTailFireConfigured = false;
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
    const glm::vec3 cameraPos = camera.getPosition();
    const glm::vec3 cameraForward = camera.getDirection();
    const glm::vec3 cameraTarget = camera.getTarget();
    const bool preferIndexedOnly =
        indexedOnlyWhenAvailable && !scratch.worldIndexedBatches.empty();

    if (!scratch.worldBackgroundQuads.empty()) {
        backendRenderer->drawDebugQuads(
            scratch.worldBackgroundQuads.data(),
            scratch.worldBackgroundQuads.size(),
            surfaceWidth,
            surfaceHeight);
    }
    if (!preferIndexedOnly && !scratch.worldTriangles.empty()) {
        backendRenderer->drawDebugTriangles(
            scratch.worldTriangles.data(),
            scratch.worldTriangles.size(),
            surfaceWidth,
            surfaceHeight);
    }
    if (!preferIndexedOnly && !scratch.world3DTriangles.empty()) {
        backendRenderer->drawWorldTriangles(
            scratch.world3DTriangles.data(),
            scratch.world3DTriangles.size(),
            glm::value_ptr(viewProj),
            surfaceWidth,
            surfaceHeight);
    }
    if (!scratch.worldIndexedBatches.empty()) {
        game::runtime::shared_world_batches::submitWorldIndexedBatches(
            *backendRenderer,
            scratch.worldIndexedBatches,
            glm::value_ptr(viewProj),
            surfaceWidth,
            surfaceHeight,
            glm::value_ptr(cameraPos),
            glm::value_ptr(cameraForward),
            glm::value_ptr(cameraTarget));
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
            .ensureBackendMeshLoaded =
                [&](const std::string& modelPath) -> game::runtime::render_model::MeshData* {
                    return ensureBackendMeshLoaded(modelPath);
                },
            .ensureBackendTextureLoaded =
                [&](const std::string& texturePath, bool flipVertical)
                    -> game::runtime::SharedBackendTextureCacheEntry* {
                    return ensureBackendTextureLoaded(texturePath, flipVertical);
                },
        },
        projectedDebug,
        scratch);
}

bool PokemonAutochessVfxPreviewProject::Impl::appendModelBatches(
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
    unit.side = side;
    unit.alive = true;
    unit.fainting = false;
    unit.captureInProgress = false;
    unit.position = worldPos;
    unit.rotation = glm::vec3(0.0f, yawDeg, 0.0f);
    unit.visualYOffset = 0.0f;
    unit.activeAnimIndex = visual.currentAnimIndex();
    unit.currentAttackAnimIndex = visual.previewAnimationActive() ? visual.currentAnimIndex() : -1;
    unit.animTimeSec = visual.currentAnimTimeSec();
    unit.visualScale = 1.0f;
    unit.captureScale = 1.0f;

    game::runtime::shared_backend_pose::PoseEval scenePose =
        game::runtime::shared_backend_pose::evaluateScenePose(*mesh, unit);

    IRenderBackend::DebugQuad tint{};
    game::runtime::render_prep_units::applyWorldUnitTint(tint, unit);

    const float minDim = static_cast<float>(std::min(surfaceWidth, surfaceHeight));
    const float cellSize = boardCellSize();
    const glm::vec3 proxyCenter = glm::vec3(worldPos.x, worldPos.y, worldPos.z);
    const glm::vec3 coarseWorldPos =
        proxyCenter + glm::vec3(0.0f, std::max(0.2f, cellSize * 0.22f), 0.0f);
    const float unitSizePx =
        computeProjectedUnitSizePx(projectedDebug, coarseWorldPos, cellSize, minDim);
    std::size_t remainingModelTrianglesBudget = 60000u;

    game::runtime::shared_projected_unit_models::PerfBreakdown perf{};
    const auto result = game::runtime::shared_projected_unit_models::renderProjectedUnitModel(
        game::runtime::shared_projected_unit_models::Args{
            .renderer = nullptr,
            .dataDb = &previewDataDb,
            .unit = &unit,
            .pose = []() -> const game::runtime::render_prep_pose::ProceduralPose* {
                static const game::runtime::render_prep_pose::ProceduralPose kIdentityPose{};
                return &kIdentityPose;
            }(),
            .meshForUnit = mesh,
            .scenePose = &scenePose,
            .backendId = backendRenderer ? backendRenderer->backendId() : nullptr,
            .scenePoseReady = true,
            .enableClipSkinning = true,
            .enableGpuClipSkinning = false,
            .tint = &tint,
            .worldCellSize = cellSize,
            .boardSurfaceY = 0.006f,
            .unitSize = unitSizePx,
            .animPitch = 0.0f,
            .animYaw = yawDeg,
            .animRoll = 0.0f,
            .attackPulse = 1.0f,
            .materialTimeSec = unit.animTimeSec,
            .renderVisualScale = 1.0f,
            .renderCaptureScale = 1.0f,
            .captureVisualTintStrength = 0.0f,
            .modelFadeAlpha = 1.0f,
            .captureTintColor = glm::vec3(1.0f),
            .proxyCenter = proxyCenter,
            .cameraWorldPos = camera.getPosition(),
            .supportsWorldTriangles3D = true,
            .supportsWorldIndexedMeshes = true,
            .characterInkingEnabled = true,
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
            .backendModelTriangleLimit = []() { return static_cast<std::size_t>(90000u); },
            .backendModelFullMeshEnabled = []() { return true; },
            .backendModelFastTexturedPathEnabled = []() { return true; },
            .backendModelBackfaceCullingEnabled = []() { return true; },
            .perfBreakdown = &perf,
        });

    game::runtime::shared_projected_scene::flushModelDepthBuffers(
        modelDepthTris,
        modelDepthWorldTris,
        scratch.worldTriangles,
        scratch.world3DTriangles);

    return result.drewModelMesh ||
           !scratch.worldIndexedBatches.empty() ||
           !scratch.world3DTriangles.empty() ||
           !scratch.worldTriangles.empty() ||
           !scratch.worldBackgroundQuads.empty();
}

PokemonAutochessVfxPreviewProject::PokemonAutochessVfxPreviewProject()
    : board_(nullptr)
    , impl_(std::make_unique<Impl>()) {
    effects_.push_back(std::make_unique<GrowlPreviewEffect>());
    effects_.push_back(std::make_unique<LeechSeedPreviewEffect>());
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
    return false;
}

void PokemonAutochessVfxPreviewProject::onEffectActivated(std::size_t effectIndex) {
    impl_->activeEffectIndex = effectIndex;
    impl_->pendingReplayAction = Impl::PendingReplayAction::None;
    impl_->pendingReplayTriggerAnimTimeSec = 0.0f;
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
    impl_->attackerVisual.clearPreviewAnimation();
    impl_->targetVisual.clearPreviewAnimation();

    if (static_cast<RigKind>(impl_->activeRigIndex) != RigKind::PokemonModels) {
        effectAt(effectIndex).replay(scene);
        impl_->pendingReplayAction = Impl::PendingReplayAction::None;
        return;
    }

    engine::tools::vfx_preview::IVfxPreviewEffect* activeEffect =
        effectIndex < effects_.size() ? effects_[effectIndex].get() : nullptr;
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
    const float attackAnimSpeed =
        (windowSec > 0.0f && clipDur > 0.0f) ? (clipDur / windowSec) : 1.0f;
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
    impl_->attackerVisual.clearPreviewAnimation();
    impl_->targetVisual.clearPreviewAnimation();

    if (static_cast<RigKind>(impl_->activeRigIndex) != RigKind::PokemonModels) {
        effectAt(effectIndex).reload(scene);
        impl_->pendingReplayAction = Impl::PendingReplayAction::None;
        return;
    }

    engine::tools::vfx_preview::IVfxPreviewEffect* activeEffect =
        effectIndex < effects_.size() ? effects_[effectIndex].get() : nullptr;
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
    const float attackAnimSpeed =
        (windowSec > 0.0f && clipDur > 0.0f) ? (clipDur / windowSec) : 1.0f;
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
    impl_->ensureTailFireConfigured();
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

    const glm::vec3 casterPos(scene.emitter.x, 0.0f, scene.emitter.z);
    const glm::vec3 targetPos(scene.target.x, 0.0f, scene.target.z);
    const float attackerYaw = computeYawDegreesFromForward(targetPos - casterPos);
    std::vector<PokemonInstance> boardUnits;
    boardUnits.reserve(1u);
    boardUnits.push_back(makePreviewRuntimeUnit(
        impl_->attackerVisual,
        casterPos,
        attackerYaw,
        PokemonSide::Player));
    static const std::vector<PokemonInstance> kNoBenchUnits;
    if (impl_->previewTailFireConfigured && impl_->previewTailFire) {
        impl_->previewTailFire->update(std::max(0.0f, dt), boardUnits, kNoBenchUnits);
    }

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
    impl_->attackerVisual.draw(frame.camera, casterPos, attackerYaw);
    impl_->targetVisual.draw(frame.camera, targetPos, targetYaw);
    impl_->renderTailFirePreview(frame.camera);
}

void PokemonAutochessVfxPreviewProject::appendDebugMarkers(
    engine::tools::vfx_preview::IPreviewDebugDraw& draw,
    const engine::tools::vfx_preview::PreviewSceneState& scene) const {
    const glm::vec3 emitterColor(1.0f, 0.52f, 0.16f);
    const glm::vec3 targetColor(0.28f, 0.95f, 0.55f);
    const glm::vec3 guideColor(0.95f, 0.90f, 0.35f);

    if (scene.showEmitterMarker) {
        draw.addCross(scene.emitter, 0.16f, emitterColor);
        draw.addCircleXZ(glm::vec3(scene.emitter.x, 0.015f, scene.emitter.z), 0.20f, emitterColor, 28);
    }

    if (scene.showTargetMarker) {
        draw.addCross(scene.target, 0.18f, targetColor);
        draw.addCircleXZ(glm::vec3(scene.target.x, 0.015f, scene.target.z), 0.24f, targetColor, 28);
    }

    if (scene.showOrientationGuide) {
        const glm::vec3 guideStart = scene.emitter + glm::vec3(0.0f, 0.02f, 0.0f);
        const glm::vec3 guideEnd = glm::vec3(scene.target.x, guideStart.y, scene.target.z);
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
            "3D Models mode uses the same board spacing and model scaling rules as gameplay.",
            "Charmander plays the Growl attack clip at gameplay speed, and the VFX fires on the same clip-time hit frame the game uses."
        };
    }
    return {};
}

} // namespace game::preview
