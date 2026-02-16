// src/game/scripting/ScriptAPI.cpp

#include "game/scripting/ScriptAPI.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/glm.hpp>

#include "game/GameWorld.h"

#include "game/animation/FlightLocomotion.h"

#include "game/config/GameDataDb.h"

#include "LuaBindings_Internal.h"

namespace {
bool isCombatActive(const PokemonInstance& u) {
    return u.alive && !u.captureInProgress;
}
} // namespace

ScriptAPI::ScriptAPI(GameWorld* world, GameStateManager* manager, GameServices& services)
    : world_(world), manager_(manager), services_(services) {}

LogBus::Logger& ScriptAPI::logger() const { return services_.log; }
ScriptEventBus& ScriptAPI::events() const { return services_.events; }
const GameConfigData& ScriptAPI::config() const { return services_.config; }

std::vector<ScriptAPI::UnitSnapshot> ScriptAPI::listUnits() const {
    std::vector<UnitSnapshot> out;
    if (!world_) return out;
    auto& list = world_->getPokemons();
    out.reserve(list.size());
    for (const auto& u : list) {
        UnitSnapshot s;
        s.id = u.id;
        s.name = u.name;
        s.side = u.side;
        s.hp = u.hp;
        s.attack = u.attack;
        s.speed = u.movementSpeed;
        s.energy = u.energy;
        s.maxEnergy = u.maxEnergy;
        auto cell = world_->worldToGrid(u.position);
        s.col = cell.x;
        s.row = cell.y;
        const bool active = isCombatActive(u);
        s.alive = active;
        s.fainting = u.fainting;
        s.blocksTile = active || u.captureInProgress || (u.fainting && config().faintBlockTiles);
        s.captureInProgress = u.captureInProgress;
        s.fastMove = u.fastMove;
        s.chargedMove = u.chargedMove;
        s.types = u.types;
        out.push_back(std::move(s));
    }
    return out;
}

std::optional<ScriptAPI::UnitSnapshot> ScriptAPI::getUnitSnapshot(int unitId) const {
    if (!world_) return std::nullopt;
    auto* u = world_->findUnitById(unitId);
    if (!u) return std::nullopt;

    UnitSnapshot s;
    s.id = u->id;
    s.name = u->name;
    s.side = u->side;
    s.hp = u->hp;
    s.attack = u->attack;
    s.speed = u->movementSpeed;
    s.energy = u->energy;
    s.maxEnergy = u->maxEnergy;
    auto cell = world_->worldToGrid(u->position);
    s.col = cell.x;
    s.row = cell.y;
    const bool active = isCombatActive(*u);
    s.alive = active;
    s.fainting = u->fainting;
    s.blocksTile = active || u->captureInProgress || (u->fainting && config().faintBlockTiles);
    s.captureInProgress = u->captureInProgress;
    s.fastMove = u->fastMove;
    s.chargedMove = u->chargedMove;
    s.types = u->types;
    return s;
}

std::pair<int, int> ScriptAPI::nearestEnemyCell(int unitId) const {
    if (!world_) return std::make_pair(-1, -1);

    auto& list = world_->getPokemons();
    const auto it = std::find_if(list.begin(), list.end(),
        [&](const PokemonInstance& p){ return p.id == unitId; });
    if (it == list.end()) return std::make_pair(-1, -1);

    if (!isCombatActive(*it)) return std::make_pair(-1, -1);
    const auto myCell = world_->worldToGrid(it->position);

    int best = std::numeric_limits<int>::max();
    glm::ivec2 bestCell(-1, -1);

    for (const auto& u : list) {
        if (!isCombatActive(u) || u.side == it->side) continue;
        const auto ec = world_->worldToGrid(u.position);
        const int d = std::max(std::abs(myCell.x - ec.x), std::abs(myCell.y - ec.y));
        if (d < best) {
            best = d;
            bestCell = ec;
        }
    }

    return std::make_pair(bestCell.x, bestCell.y);
}

bool ScriptAPI::isAdjacentToEnemy(int unitId) const {
    if (!world_) return false;
    auto& list = world_->getPokemons();
    auto it = std::find_if(list.begin(), list.end(),
        [&](const PokemonInstance& p){ return p.id == unitId; });
    if (it == list.end()) return false;
    if (!isCombatActive(*it)) return false;
    auto myCell = world_->worldToGrid(it->position);

    int best = std::numeric_limits<int>::max();
    glm::ivec2 bestCell(-999,-999);
    for (auto& u : list) {
        if (!isCombatActive(u) || u.side == it->side) continue;
        auto ec = world_->worldToGrid(u.position);
        const int d = std::max(std::abs(myCell.x - ec.x), std::abs(myCell.y - ec.y));
        if (d < best) { best = d; bestCell = ec; }
    }
    const int dx = std::abs(myCell.x - bestCell.x);
    const int dy = std::abs(myCell.y - bestCell.y);
    return std::max(dx, dy) == 1;
}

std::vector<int> ScriptAPI::enemiesAdjacent(int unitId) const {
    std::vector<int> out;
    if (!world_) return out;

    PokemonInstance* attacker = nullptr;
    for (auto& u : world_->getPokemons()) if (u.id == unitId) { attacker = &u; break; }
    if (!attacker || !isCombatActive(*attacker)) return out;

    auto ac = world_->worldToGrid(attacker->position);
    for (auto& u : world_->getPokemons()) {
        if (!isCombatActive(u) || u.side == attacker->side) continue;
        auto ec = world_->worldToGrid(u.position);
        const int dx = std::abs(ac.x - ec.x);
        const int dy = std::abs(ac.y - ec.y);
        if (std::max(dx, dy) == 1) {
            out.push_back(u.id);
        }
    }
    return out;
}

bool ScriptAPI::canAttack(int unitId) const {
    if (!world_) return false;
    for (auto& u : world_->getPokemons()) {
        if (u.id != unitId) continue;
        if (!isCombatActive(u)) return false;
        if (u.usesAirLocomotion && FlightLocomotion::isAirborne(u)) return false;
        return true;
    }
    return false;
}

bool ScriptAPI::attackReady(int unitId) const {
    if (!world_) return false;
    for (auto& u : world_->getPokemons()) {
        if (u.id != unitId) continue;
        if (!isCombatActive(u)) return false;
        if (u.usesAirLocomotion && FlightLocomotion::isAirborne(u)) return false;
        if (u.attackTimerSec > 0.0001f) return false;
        return true;
    }
    return false;
}

float ScriptAPI::attackMinRequestSec(int attackerId,
                                     const std::optional<std::string>& moveName,
                                     const std::optional<std::string>& kind) const {
    if (!world_) return 0.0f;
    auto& list = world_->getPokemons();
    auto A = std::find_if(list.begin(), list.end(),
        [&](const PokemonInstance& p){ return p.id == attackerId; });
    if (A == list.end()) return 0.0f;

    const std::string speciesLower = toLowerCopy(A->name);
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
    if (auto* u = world_->findUnitById(unitId)) return u->energy;
    return 0;
}

int ScriptAPI::getMaxEnergy(int unitId) const {
    if (!world_) return 100;
    if (auto* u = world_->findUnitById(unitId)) return u->maxEnergy;
    return 100;
}

float ScriptAPI::getUnitSpeed(int unitId) const {
    if (!world_) return 0.0f;
    if (auto* u = world_->findUnitById(unitId)) return u->movementSpeed;
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
    auto* A = world_->findUnitById(attackerId);
    auto* T = world_->findUnitById(targetId);
    if (!A || !T) return 1.0f;

    const auto& b = world_->getCombatBalance();

    const float attMult = (A->side == PokemonSide::Player) ? b.playerDamageMult : b.enemyDamageMult;
    const float takenMult = (T->side == PokemonSide::Player) ? b.playerDamageTakenMult : b.enemyDamageTakenMult;

    const float safeAtt = std::max(0.0f, attMult);
    const float safeTaken = std::max(0.0f, takenMult);
    const float out = safeAtt * safeTaken;
    return (out > 0.0f) ? out : 0.0f;
}

std::string ScriptAPI::getUnitFastMove(int unitId) const {
    if (!world_) return {};
    if (auto* u = world_->findUnitById(unitId)) return u->fastMove;
    return {};
}

std::string ScriptAPI::getUnitChargedMove(int unitId) const {
    if (!world_) return {};
    if (auto* u = world_->findUnitById(unitId)) return u->chargedMove;
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
    if (auto* u = world_->findUnitById(unitId)) {
        return (u->committedDest.x >= 0 && u->committedDest.y >= 0);
    }
    return false;
}

bool ScriptAPI::isMoving(int unitId) const {
    if (!world_) return false;
    if (auto* u = world_->findUnitById(unitId)) return u->isMoving;
    return false;
}
