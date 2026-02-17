// src/game/scripting/ScriptAPI.cpp

#include "game/scripting/ScriptAPI.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <glm/glm.hpp>

#include "game/GameWorld.h"
#include "game/animation/FlightLocomotion.h"
#include "game/config/GameDataDb.h"

#include "LuaBindings_Internal.h"

namespace {

constexpr float kAttackReadyEps = 0.0001f;

bool isCombatActive(const PokemonInstance& unit) {
    return unit.alive && !unit.captureInProgress;
}

bool canIssueAttack(const PokemonInstance& unit) {
    if (!isCombatActive(unit)) return false;
    if (unit.usesAirLocomotion && FlightLocomotion::isAirborne(unit)) return false;
    return true;
}

ScriptAPI::UnitSnapshot makeUnitSnapshot(const GameWorld& world,
                                         const GameConfigData& config,
                                         const PokemonInstance& unit) {
    ScriptAPI::UnitSnapshot snapshot;
    snapshot.id = unit.id;
    snapshot.name = unit.name;
    snapshot.side = unit.side;
    snapshot.hp = unit.hp;
    snapshot.attack = unit.attack;
    snapshot.speed = unit.movementSpeed;
    snapshot.energy = unit.energy;
    snapshot.maxEnergy = unit.maxEnergy;

    const auto cell = world.worldToGrid(unit.position);
    snapshot.col = cell.x;
    snapshot.row = cell.y;

    const bool active = isCombatActive(unit);
    snapshot.alive = active;
    snapshot.fainting = unit.fainting;
    snapshot.blocksTile = active || unit.captureInProgress || (unit.fainting && config.faintBlockTiles);
    snapshot.captureInProgress = unit.captureInProgress;
    snapshot.fastMove = unit.fastMove;
    snapshot.chargedMove = unit.chargedMove;
    snapshot.types = unit.types;
    return snapshot;
}
}  // namespace

ScriptAPI::ScriptAPI(GameWorld* world, GameStateManager* manager, GameServices& services)
    : world_(world), manager_(manager), services_(services) {}

LogBus::Logger& ScriptAPI::logger() const { return services_.log; }
ScriptEventBus& ScriptAPI::events() const { return services_.events; }
const GameConfigData& ScriptAPI::config() const { return services_.config; }

std::vector<ScriptAPI::UnitSnapshot> ScriptAPI::listUnits() const {
    std::vector<UnitSnapshot> out;
    if (!world_) return out;
    const auto& units = world_->getPokemons();
    out.reserve(units.size());
    for (const auto& unit : units) {
        out.push_back(makeUnitSnapshot(*world_, config(), unit));
    }
    return out;
}

std::optional<ScriptAPI::UnitSnapshot> ScriptAPI::getUnitSnapshot(int unitId) const {
    if (!world_) return std::nullopt;
    const auto* unit = world_->findUnitById(unitId);
    if (!unit) return std::nullopt;
    return makeUnitSnapshot(*world_, config(), *unit);
}

std::pair<int, int> ScriptAPI::nearestEnemyCell(int unitId) const {
    if (!world_) return {-1, -1};
    const auto* unit = world_->findUnitById(unitId);
    if (!unit || !isCombatActive(*unit)) return {-1, -1};

    const auto myCell = world_->worldToGrid(unit->position);

    int best = std::numeric_limits<int>::max();
    glm::ivec2 bestCell(-1, -1);

    for (const auto& candidate : world_->getPokemons()) {
        if (!isCombatActive(candidate) || candidate.side == unit->side) continue;
        const auto ec = world_->worldToGrid(candidate.position);
        const int d = std::max(std::abs(myCell.x - ec.x), std::abs(myCell.y - ec.y));
        if (d < best) {
            best = d;
            bestCell = ec;
        }
    }

    return {bestCell.x, bestCell.y};
}

bool ScriptAPI::isAdjacentToEnemy(int unitId) const {
    if (!world_) return false;
    const auto* unit = world_->findUnitById(unitId);
    if (!unit || !isCombatActive(*unit)) return false;

    const auto myCell = world_->worldToGrid(unit->position);
    const auto nearest = nearestEnemyCell(unitId);
    if (nearest.first < 0 || nearest.second < 0) return false;

    const int dx = std::abs(myCell.x - nearest.first);
    const int dy = std::abs(myCell.y - nearest.second);
    return std::max(dx, dy) == 1;
}

std::vector<int> ScriptAPI::enemiesAdjacent(int unitId) const {
    std::vector<int> out;
    if (!world_) return out;

    const auto* attacker = world_->findUnitById(unitId);
    if (!attacker || !isCombatActive(*attacker)) return out;

    const auto ac = world_->worldToGrid(attacker->position);
    for (const auto& candidate : world_->getPokemons()) {
        if (!isCombatActive(candidate) || candidate.side == attacker->side) continue;
        const auto ec = world_->worldToGrid(candidate.position);
        const int dx = std::abs(ac.x - ec.x);
        const int dy = std::abs(ac.y - ec.y);
        if (std::max(dx, dy) == 1) {
            out.push_back(candidate.id);
        }
    }
    return out;
}

bool ScriptAPI::canAttack(int unitId) const {
    if (!world_) return false;
    const auto* unit = world_->findUnitById(unitId);
    return unit && canIssueAttack(*unit);
}

bool ScriptAPI::attackReady(int unitId) const {
    if (!world_) return false;
    const auto* unit = world_->findUnitById(unitId);
    if (!unit || !canIssueAttack(*unit)) return false;
    return unit->attackTimerSec <= kAttackReadyEps;
}

float ScriptAPI::attackMinRequestSec(int attackerId,
                                     const std::optional<std::string>& moveName,
                                     const std::optional<std::string>& kind) const {
    if (!world_) return 0.0f;
    const auto* attacker = world_->findUnitById(attackerId);
    if (!attacker) return 0.0f;

    const std::string speciesLower = toLowerCopy(attacker->name);
    const std::string moveLower    = moveName ? toLowerCopy(*moveName) : "";
    std::string kindLower          = kind ? toLowerCopy(*kind) : "";

    const auto& data = services_.dataDb;
    if (kindLower.empty() && !moveLower.empty()) {
        if (const MoveData* md = data.moves.getMove(moveLower)) {
            kindLower = toLowerCopy(md->kind);
        }
    }
    if (kindLower.empty()) kindLower = "fast";

    return data.attackAnims.getMinRequestSec(speciesLower, kindLower, moveLower, &services_.log);
}

int ScriptAPI::getEnergy(int unitId) const {
    if (!world_) return 0;
    if (const auto* unit = world_->findUnitById(unitId)) return unit->energy;
    return 0;
}

int ScriptAPI::getMaxEnergy(int unitId) const {
    if (!world_) return 100;
    if (const auto* unit = world_->findUnitById(unitId)) return unit->maxEnergy;
    return 100;
}

float ScriptAPI::getUnitSpeed(int unitId) const {
    if (!world_) return 0.0f;
    if (const auto* unit = world_->findUnitById(unitId)) return unit->movementSpeed;
    return 0.0f;
}

std::tuple<float, float, float> ScriptAPI::gridToWorldPos(int col, int row) const {
    if (!world_) {
        auto p = gridToWorld(config(), col, row);
        return std::make_tuple(p.x, p.y, p.z);
    }
    const auto p = world_->gridToWorld(col, row);
    return std::make_tuple(p.x, p.y, p.z);
}

std::pair<int, int> ScriptAPI::worldToGridPos(float x, float y, float z) const {
    if (!world_) {
        auto c = worldToGrid(config(), glm::vec3{x, y, z});
        return std::make_pair(c.x, c.y);
    }
    const auto c = world_->worldToGrid(glm::vec3{x, y, z});
    return std::make_pair(c.x, c.y);
}

float ScriptAPI::getDamageMultiplier(int attackerId, int targetId) const {
    if (!world_) return 1.0f;
    const auto* attacker = world_->findUnitById(attackerId);
    const auto* target = world_->findUnitById(targetId);
    if (!attacker || !target) return 1.0f;

    const auto& b = world_->getCombatBalance();

    const float attMult = (attacker->side == PokemonSide::Player) ? b.playerDamageMult : b.enemyDamageMult;
    const float takenMult = (target->side == PokemonSide::Player) ? b.playerDamageTakenMult : b.enemyDamageTakenMult;

    const float safeAtt = std::max(0.0f, attMult);
    const float safeTaken = std::max(0.0f, takenMult);
    const float out = safeAtt * safeTaken;
    return (out > 0.0f) ? out : 0.0f;
}

std::string ScriptAPI::getUnitFastMove(int unitId) const {
    if (!world_) return {};
    if (const auto* unit = world_->findUnitById(unitId)) return unit->fastMove;
    return {};
}

std::string ScriptAPI::getUnitChargedMove(int unitId) const {
    if (!world_) return {};
    if (const auto* unit = world_->findUnitById(unitId)) return unit->chargedMove;
    return {};
}

std::optional<ScriptAPI::MoveSnapshot> ScriptAPI::getMove(const std::string& name) const {
    const auto& data = services_.dataDb;
    const MoveData* md = data.moves.getMove(name);
    if (!md) return std::nullopt;

    MoveSnapshot m;
    m.name = md->name;
    m.type = md->type;
    m.kind = md->kind;
    m.cooldownSec = md->cooldownSec;
    m.power = md->power;
    m.range = md->range;
    m.energyGain = md->energyGain;
    m.energyCost = md->energyCost;
    if (md->status.valid) {
        m.status.effect = md->status.effect;
        m.status.magnitude = md->status.magnitude;
        m.status.durationSec = md->status.durationSec;
        m.status.target = md->status.target;
        m.status.valid = true;
    }
    return m;
}

bool ScriptAPI::hasPlannedMove(int unitId) const {
    if (!world_) return false;
    if (const auto* unit = world_->findUnitById(unitId)) {
        return (unit->committedDest.x >= 0 && unit->committedDest.y >= 0);
    }
    return false;
}

bool ScriptAPI::isMoving(int unitId) const {
    if (!world_) return false;
    if (const auto* unit = world_->findUnitById(unitId)) return unit->isMoving;
    return false;
}
