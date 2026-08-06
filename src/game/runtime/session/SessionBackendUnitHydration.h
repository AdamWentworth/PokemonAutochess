#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/PokemonInstance.h"
#include "game/config/GameDataDb.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"

namespace game::runtime::session_backend_unit_hydration {

struct BackendAnimRoleEntry {
    bool attemptedResolve = false;
    int idleIndex = -1;
    int moveIndex = -1;
    int attackIndex = -1;
    int groundIdleIndex = -1;
    int airIdleIndex = -1;
    int takeoffIndex = -1;
    int takeoffLoopIndex = -1;
    int landIndex = -1;
    int landAIndex = -1;
    int landBIndex = -1;
    int landCIndex = -1;
    int faintIndex = -1;
    float animFps = 24.0f;
    float attackDurationSec = 0.0f;
    float faintDurationSec = 0.0f;
    bool usesAirLocomotion = false;
    float airLiftY = 0.0f;
    float takeoffSec = 0.0f;
    float landingSec = 0.0f;
};

using BackendAnimRoleCache = std::unordered_map<std::string, BackendAnimRoleEntry>;
using EnsureBackendMeshLoadedFn = std::function<game::runtime::render_model::MeshData*(const std::string&)>;

BackendAnimRoleEntry& ensureBackendAnimRoles(const std::string& modelPath,
                                             const game::runtime::render_model::MeshData* mesh,
                                             BackendAnimRoleCache& cache);

void hydrateBackendUnits(std::vector<PokemonInstance>& boardUnits,
                         std::vector<PokemonInstance>& benchUnits,
                         const GameDataDb& dataDb,
                         BackendAnimRoleCache& cache,
                         const EnsureBackendMeshLoadedFn& ensureBackendMeshLoaded);

} // namespace game::runtime::session_backend_unit_hydration
