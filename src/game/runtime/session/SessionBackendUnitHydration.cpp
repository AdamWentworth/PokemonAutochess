#include "game/runtime/session/SessionBackendUnitHydration.h"

#include <algorithm>
#include <nlohmann/json.hpp>

#include "game/config/AnimSetLoader.h"
#include "game/runtime/session/SessionBackendRenderHelpers.h"

namespace {

void hydrateUnit(PokemonInstance& unit,
                 const GameDataDb& dataDb,
                 game::runtime::session_backend_unit_hydration::BackendAnimRoleCache& cache,
                 const game::runtime::session_backend_unit_hydration::EnsureBackendMeshLoadedFn& ensureBackendMeshLoaded) {
    if (!ensureBackendMeshLoaded) return;

    const PokemonStats* stats = dataDb.pokemon.getStats(unit.name);
    std::string modelPath = unit.backendModelPath;
    if (modelPath.empty()) {
        if (!stats || stats->resolveModel(unit.modelVariant).empty()) {
            unit.backendModelPath.clear();
            return;
        }
        modelPath = "assets/models/" + stats->resolveModel(unit.modelVariant);
        unit.backendModelPath = modelPath;
    }
    game::runtime::render_model::MeshData* mesh = ensureBackendMeshLoaded(modelPath);
    if (!mesh) return;

    if (!unit.model) {
        if (unit.animIndexCacheSourceModelPath != modelPath || unit.animIndexCache.empty()) {
            unit.animIndexCache.clear();
            unit.animIndexCache.reserve(std::max<std::size_t>(16u, mesh->animations.size() * 8u));
            unit.animIndexCacheSourceModelPath = modelPath;
            const auto cacheAlias = [&](const std::string& clipName, int idx) {
                if (clipName.empty() || idx < 0) return;
                unit.animIndexCache[clipName] = idx;
                unit.animIndexCache[game::runtime::session_backend_render_helpers::toLowerCopy(clipName)] = idx;
            };

            for (std::size_t i = 0; i < mesh->animations.size(); ++i) {
                const int idx = static_cast<int>(i);
                const std::string& raw = mesh->animations[i].name;
                cacheAlias(raw, idx);
                const std::string noGfbanm = game::runtime::session_backend_render_helpers::stripSuffix(raw, ".gfbanm");
                cacheAlias(noGfbanm, idx);
                const std::string noStart = game::runtime::session_backend_render_helpers::stripSuffix(raw, "__START");
                cacheAlias(noStart, idx);
                const std::string noEnd = game::runtime::session_backend_render_helpers::stripSuffix(raw, "__END");
                cacheAlias(noEnd, idx);
                std::string compact = game::runtime::session_backend_render_helpers::stripSuffix(noGfbanm, "__START");
                compact = game::runtime::session_backend_render_helpers::stripSuffix(compact, "__END");
                cacheAlias(compact, idx);
            }
        }
        if (unit.backendAnimDurationsSourceModelPath != modelPath ||
            unit.backendAnimDurationsSec.size() != mesh->animations.size()) {
            unit.backendAnimDurationsSec.assign(mesh->animations.size(), 0.0f);
            for (std::size_t i = 0; i < mesh->animations.size(); ++i) {
                unit.backendAnimDurationsSec[i] =
                    std::max(0.0f, mesh->animations[i].durationSec);
            }
            unit.backendAnimDurationsSourceModelPath = modelPath;
        }
    } else {
        unit.animIndexCacheSourceModelPath.clear();
        unit.backendAnimDurationsSourceModelPath.clear();
        unit.backendAnimDurationsSec.clear();
    }

    auto& roles = game::runtime::session_backend_unit_hydration::ensureBackendAnimRoles(modelPath, mesh, cache);

    const bool backendOnlyUnit = !unit.model;
    const auto assignRoleIndex = [&](int& dst, int src) {
        if (backendOnlyUnit || dst < 0) {
            dst = src;
        }
    };

    assignRoleIndex(unit.animIdleIndex, roles.idleIndex);
    assignRoleIndex(unit.animMoveIndex, roles.moveIndex);
    assignRoleIndex(unit.animAttack1Index, roles.attackIndex);
    assignRoleIndex(unit.animFaintIndex, roles.faintIndex);
    assignRoleIndex(unit.animGroundIdleIndex, roles.groundIdleIndex);
    assignRoleIndex(unit.animAirIdleIndex, roles.airIdleIndex);
    assignRoleIndex(unit.animTakeoffIndex, roles.takeoffIndex);
    assignRoleIndex(unit.animLandIndex, roles.landIndex);
    assignRoleIndex(unit.animLandAIndex, roles.landAIndex);
    assignRoleIndex(unit.animLandBIndex, roles.landBIndex);
    assignRoleIndex(unit.animLandCIndex, roles.landCIndex);

    if (roles.animFps > 0.0f) {
        unit.animFps = roles.animFps;
    }
    if ((backendOnlyUnit || unit.attackDurationSec <= 0.0f) && roles.attackDurationSec > 0.0f) {
        unit.attackDurationSec = roles.attackDurationSec;
    }
    if ((backendOnlyUnit || unit.faintAnimDurationSec <= 0.0f) && roles.faintDurationSec > 0.0f) {
        unit.faintAnimDurationSec = roles.faintDurationSec;
    }

    const bool speciesListedFlyer = dataDb.flyers.isFlyer(unit.name);
    if ((roles.usesAirLocomotion || speciesListedFlyer) && !unit.usesAirLocomotion) {
        unit.usesAirLocomotion = true;
    }
    if (unit.usesAirLocomotion) {
        if (unit.airLiftY <= 0.0f && roles.airLiftY > 0.0f) unit.airLiftY = roles.airLiftY;
        if (unit.takeoffSec <= 0.0f && roles.takeoffSec > 0.0f) unit.takeoffSec = roles.takeoffSec;
        if (unit.landingSec <= 0.0f && roles.landingSec > 0.0f) unit.landingSec = roles.landingSec;
        if (const auto* d = dataDb.flyers.getAirLocomotionDefaults(unit.name)) {
            if (unit.airLiftY <= 0.0f && d->airLiftY.has_value()) {
                unit.airLiftY = *d->airLiftY;
            }
            if (unit.takeoffSec <= 0.0f && d->takeoffSec.has_value()) {
                unit.takeoffSec = *d->takeoffSec;
            }
            if (unit.landingSec <= 0.0f && d->landingSec.has_value()) {
                unit.landingSec = *d->landingSec;
            }
            if (d->takeoffAnimSpeed.has_value()) {
                unit.takeoffAnimSpeed = *d->takeoffAnimSpeed;
            }
            if (d->landAnimSpeed.has_value()) {
                unit.landAnimSpeed = *d->landAnimSpeed;
            }
        }
    }

    const bool unresolvedBackendActiveAnim =
        backendOnlyUnit &&
        unit.activeAnimIndex == 1 &&
        unit.animIdleIndex != 1;
    if (unit.activeAnimIndex < 0 || unresolvedBackendActiveAnim) {
        unit.activeAnimIndex = unit.isMoving ? unit.animMoveIndex : unit.animIdleIndex;
    }
    if (unit.activeAnimIndex < 0 && !mesh->animations.empty()) {
        unit.activeAnimIndex = 0;
    }
    if (unit.currentAttackAnimIndex < 0) {
        unit.currentAttackAnimIndex = unit.animAttack1Index;
    }

    if (stats) {
        const std::string scaleMode = game::runtime::session_backend_render_helpers::toLowerCopy(stats->modelScaleMode);
        if (!unit.model && scaleMode != "normalized") {
            const float importerScale = std::max(0.0f, mesh->modelScaleFactor);
            if (importerScale > 1e-6f) {
                unit.modelScaleCorrection = 1.0f / importerScale;
            }
        }
    }
}

} // namespace

namespace game::runtime::session_backend_unit_hydration {

BackendAnimRoleEntry& ensureBackendAnimRoles(const std::string& modelPath,
                                             const game::runtime::render_model::MeshData* mesh,
                                             BackendAnimRoleCache& cache) {
    auto& entry = cache[modelPath];
    if (entry.attemptedResolve) return entry;
    entry.attemptedResolve = true;
    if (!mesh) return entry;

    const int fallbackLoop = mesh->animations.empty() ? -1 : 0;
    entry.idleIndex = fallbackLoop;
    entry.moveIndex = fallbackLoop;
    entry.attackIndex = fallbackLoop;
    entry.groundIdleIndex = fallbackLoop;
    entry.airIdleIndex = fallbackLoop;

    nlohmann::json animSetJson;
    if (AnimSet::loadAnimSetJson(AnimSet::animSetPathFromModelPath(modelPath), animSetJson)) {
        if (animSetJson.contains("fps") && animSetJson["fps"].is_number()) {
            const float sourceFps = animSetJson["fps"].get<float>();
            if (sourceFps > 0.0f) entry.animFps = sourceFps;
        }
        const auto idlePick = AnimSet::resolveRoleClip(
            animSetJson,
            "idle",
            "idle",
            {"battlewait", "defaultwait", "kw01_wait", "idle", "wait"},
            true);
        const auto movePick = AnimSet::resolveRoleClip(
            animSetJson, "move", "move", {"run", "dash", "move"}, true);

        auto attackPick = AnimSet::resolveRoleClip(
            animSetJson, "attack1", "attack", {"attack01", "attack1", "attack"}, true);
        if (!attackPick.valid || attackPick.clipName.empty()) {
            attackPick =
                AnimSet::resolveRoleClip(animSetJson, "attack1", "misc", {"buturi", "ba20_buturi", "ba20", "tokusyu", "ba21"}, false);
        }

        auto faintPick = AnimSet::resolveRoleClip(
            animSetJson, "faint", "status", {"down01_start", "down_start", "down01", "down"}, true);
        if (!faintPick.valid || faintPick.clipName.empty()) {
            faintPick = AnimSet::resolveRoleClip(
                animSetJson, "down", "status", {"down01_start", "down_start", "down01", "down"}, true);
        }

        const auto groundIdlePick = AnimSet::resolveRoleClip(
            animSetJson, "ground_idle", "idle", {"ba10_wait", "battlewait", "ba10", "wait", "idle"}, true);
        const auto airIdlePick = AnimSet::resolveRoleClip(
            animSetJson, "air_idle", "idle", {"fi01_wait", "fly", "air", "hover"}, true);
        const auto takeoffPick = AnimSet::resolveRoleClip(
            animSetJson, "takeoff", "misc", {"take_flight", "takeflight", "takeoff"}, false);
        const auto landPick = AnimSet::resolveRoleClip(
            animSetJson, "land", "misc", {"land"}, false);
        const auto landAPick = AnimSet::resolveRoleClip(
            animSetJson, "land_a", "misc", {"landa"}, false);
        const auto landBPick = AnimSet::resolveRoleClip(
            animSetJson, "land_b", "misc", {"landb"}, false);
        const auto landCPick = AnimSet::resolveRoleClip(
            animSetJson, "land_c", "misc", {"landc"}, false);

        auto resolvePick = [&](const AnimSet::RolePick& pick) -> int {
            if (!pick.valid || pick.clipName.empty()) return -1;
            return game::runtime::session_backend_render_helpers::resolveBackendAnimIndexByName(mesh->animations, pick.clipName);
        };

        const int idleIdx = resolvePick(idlePick);
        if (idleIdx >= 0) entry.idleIndex = idleIdx;

        const int moveIdx = resolvePick(movePick);
        if (moveIdx >= 0) entry.moveIndex = moveIdx;

        const int attackIdx = resolvePick(attackPick);
        if (attackIdx >= 0) {
            entry.attackIndex = attackIdx;
            entry.attackDurationSec = attackPick.durationSec;
        }

        const int faintIdx = resolvePick(faintPick);
        if (faintIdx >= 0) {
            entry.faintIndex = faintIdx;
            entry.faintDurationSec = faintPick.durationSec;
        }

        const int groundIdleIdx = resolvePick(groundIdlePick);
        if (groundIdleIdx >= 0) entry.groundIdleIndex = groundIdleIdx;
        const int airIdleIdx = resolvePick(airIdlePick);
        if (airIdleIdx >= 0) entry.airIdleIndex = airIdleIdx;
        entry.takeoffIndex = resolvePick(takeoffPick);
        entry.landIndex = resolvePick(landPick);
        entry.landAIndex = resolvePick(landAPick);
        entry.landBIndex = resolvePick(landBPick);
        entry.landCIndex = resolvePick(landCPick);

        if (animSetJson.contains("meta") && animSetJson["meta"].is_object()) {
            const auto& meta = animSetJson["meta"];
            if (meta.contains("movementMode") && meta["movementMode"].is_string()) {
                const std::string mode = game::runtime::session_backend_render_helpers::toLowerCopy(meta["movementMode"].get<std::string>());
                entry.usesAirLocomotion =
                    (mode == "airborne" || mode == "air" || mode == "flying" || mode == "fly");
            }
            if (meta.contains("airLiftY") && meta["airLiftY"].is_number()) {
                entry.airLiftY = meta["airLiftY"].get<float>();
            }
            if (meta.contains("takeoffSec") && meta["takeoffSec"].is_number()) {
                entry.takeoffSec = meta["takeoffSec"].get<float>();
            }
            if (meta.contains("landingSec") && meta["landingSec"].is_number()) {
                entry.landingSec = meta["landingSec"].get<float>();
            }
        }
    }

    if (entry.idleIndex < 0) {
        entry.idleIndex = game::runtime::session_backend_render_helpers::findBackendAnimIndexBySubstring(mesh->animations, {"wait", "idle", "ba10"});
    }
    if (entry.moveIndex < 0) {
        entry.moveIndex = game::runtime::session_backend_render_helpers::findBackendAnimIndexBySubstring(mesh->animations, {"move", "run", "walk", "fly"});
    }
    if (entry.attackIndex < 0) {
        entry.attackIndex =
            game::runtime::session_backend_render_helpers::findBackendAnimIndexBySubstring(mesh->animations, {"attack", "ba20", "buturi", "strike"});
    }
    if (entry.faintIndex < 0) {
        entry.faintIndex = game::runtime::session_backend_render_helpers::findBackendAnimIndexBySubstring(mesh->animations, {"down", "faint", "death", "ko"});
    }

    if (entry.groundIdleIndex < 0) entry.groundIdleIndex = entry.idleIndex;
    if (entry.airIdleIndex < 0) entry.airIdleIndex = entry.idleIndex;

    if (entry.attackDurationSec <= 0.0f &&
        entry.attackIndex >= 0 &&
        static_cast<std::size_t>(entry.attackIndex) < mesh->animations.size()) {
        entry.attackDurationSec = mesh->animations[static_cast<std::size_t>(entry.attackIndex)].durationSec;
    }
    if (entry.faintDurationSec <= 0.0f &&
        entry.faintIndex >= 0 &&
        static_cast<std::size_t>(entry.faintIndex) < mesh->animations.size()) {
        entry.faintDurationSec = mesh->animations[static_cast<std::size_t>(entry.faintIndex)].durationSec;
    }

    const bool hasTakeoff = entry.takeoffIndex >= 0;
    const bool hasSeqLanding = (entry.landCIndex >= 0) && (entry.landAIndex >= 0 || entry.landBIndex >= 0);
    const bool hasSingleLanding = entry.landIndex >= 0;
    const bool hasDistinctLand =
        hasSeqLanding || (hasTakeoff && hasSingleLanding && entry.takeoffIndex != entry.landIndex);
    if (hasTakeoff && hasDistinctLand) {
        entry.usesAirLocomotion = true;
    }

    return entry;
}

void hydrateBackendUnits(std::vector<PokemonInstance>& boardUnits,
                         std::vector<PokemonInstance>& benchUnits,
                         const GameDataDb& dataDb,
                         BackendAnimRoleCache& cache,
                         const EnsureBackendMeshLoadedFn& ensureBackendMeshLoaded) {
    for (auto& unit : boardUnits) {
        hydrateUnit(unit, dataDb, cache, ensureBackendMeshLoaded);
    }
    for (auto& unit : benchUnits) {
        hydrateUnit(unit, dataDb, cache, ensureBackendMeshLoaded);
    }
}

} // namespace game::runtime::session_backend_unit_hydration
