#include "game/scripting/ScriptAPI.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

#include <glm/glm.hpp>

#include "game/GameStateManager.h"
#include "game/GameWorld.h"
#include "game/logging/LoggerUtil.h"
#include "game/state/CombatState.h"
#include "game/state/scripted/ScriptedState.h"

#include "LuaBindings_Internal.h"

namespace {

bool isCombatActive(const PokemonInstance& u) {
    return u.alive && !u.captureInProgress;
}

bool setFacingToTarget(PokemonInstance& unit, const glm::vec3& targetPos) {
    const glm::vec3 delta = targetPos - unit.position;
    const float lenSq = glm::dot(delta, delta);
    if (lenSq <= 1e-8f) return false;

    const glm::vec3 lookDir = delta / std::sqrt(lenSq);
    constexpr float kRadToDeg = 57.29577951308232f;
    unit.rotation.y = std::atan2(lookDir.x, lookDir.z) * kRadToDeg;
    return true;
}

}  // namespace

std::vector<ScriptEvent> ScriptAPI::drainEvents() {
    return services_.events.drain();
}

void ScriptAPI::enqueue(Command cmd) {
    queue_.push_back(std::move(cmd));
}

void ScriptAPI::flush() {
    for (const auto& cmd : queue_) {
        applyCommand(cmd);
    }
    queue_.clear();
}

void ScriptAPI::emit(const std::string& tagOrMsg, const std::optional<std::string>& payload) {
    EmitCommand cmd;
    cmd.tag = tagOrMsg;
    if (payload.has_value() && !payload->empty()) {
        cmd.payload = *payload;
        cmd.hasPayload = true;
    }
    enqueue(cmd);
}

void ScriptAPI::spawnPokemon(const std::string& name, float x, float y, float z) {
    SpawnCommand cmd;
    cmd.name = name;
    cmd.x = x;
    cmd.y = y;
    cmd.z = z;
    enqueue(cmd);
}

void ScriptAPI::spawnOnGrid(const std::string& name, int col, int row, PokemonSide side, int level) {
    SpawnOnGridCommand cmd;
    cmd.name = name;
    cmd.col = col;
    cmd.row = row;
    cmd.side = side;
    cmd.level = level;
    enqueue(cmd);
}

void ScriptAPI::emitCatch(const std::string& msg) {
    if (msg.empty()) return;
    services_.log.catchInfo(msg);
}

void ScriptAPI::emitGold(const std::string& msg) {
    if (msg.empty()) return;
    services_.log.economyInfo(msg);
}

void ScriptAPI::pushState(const std::string& scriptPath) {
    PushStateCommand cmd;
    cmd.scriptPath = scriptPath;
    enqueue(cmd);
}

void ScriptAPI::pushCombatState(const std::string& scriptPath) {
    PushCombatStateCommand cmd;
    cmd.scriptPath = scriptPath;
    enqueue(cmd);
}

void ScriptAPI::popState() {
    enqueue(PopStateCommand{});
}

void ScriptAPI::addToBench(const std::string& name, int level) {
    AddToBenchCommand cmd;
    cmd.name = name;
    cmd.level = level;
    enqueue(cmd);
}

bool ScriptAPI::applyMove(int unitId, int col, int row) {
    if (!world_) return false;
    auto* u = world_->findUnitById(unitId);
    if (!u || !isCombatActive(*u)) return false;

    ApplyMoveCommand cmd;
    cmd.unitId = unitId;
    cmd.col = col;
    cmd.row = row;
    enqueue(cmd);
    return true;
}

bool ScriptAPI::commitMove(int unitId, int col, int row) {
    if (!world_) return false;
    auto* u = world_->findUnitById(unitId);
    if (!u || !isCombatActive(*u)) return false;

    CommitMoveCommand cmd;
    cmd.unitId = unitId;
    cmd.col = col;
    cmd.row = row;
    enqueue(cmd);
    return true;
}

void ScriptAPI::faceEnemy(int unitId, const std::optional<int>& tgtCol, const std::optional<int>& tgtRow) {
    FaceEnemyCommand cmd;
    cmd.unitId = unitId;
    cmd.hasTarget = (tgtCol.has_value() && tgtRow.has_value());
    if (cmd.hasTarget) {
        cmd.col = *tgtCol;
        cmd.row = *tgtRow;
    }
    enqueue(cmd);
}

void ScriptAPI::faceTarget(int unitId, int targetId) {
    FaceTargetCommand cmd;
    cmd.unitId = unitId;
    cmd.targetId = targetId;
    enqueue(cmd);
}

bool ScriptAPI::setEnergy(int unitId, int value) {
    if (!world_) return false;
    if (!world_->findUnitById(unitId)) return false;

    SetEnergyCommand cmd;
    cmd.unitId = unitId;
    cmd.value = value;
    enqueue(cmd);
    return true;
}

int ScriptAPI::addEnergy(int unitId, int delta) {
    if (!world_) return 0;
    auto* u = world_->findUnitById(unitId);
    if (!u) return 0;

    AddEnergyCommand cmd;
    cmd.unitId = unitId;
    cmd.delta = delta;
    enqueue(cmd);
    const int m = u->maxEnergy;
    return std::max(0, std::min(u->energy + delta, m));
}

void ScriptAPI::applyCommand(const Command& cmd) {
    if (std::holds_alternative<EmitCommand>(cmd)) {
        const auto& c = std::get<EmitCommand>(cmd);
        if (c.hasPayload) {
            services_.events.emit(c.tag, c.payload);
        } else {
            services_.events.emit("log", c.tag);
        }
        if (c.hasPayload) {
            const bool hasBrackets = !c.tag.empty() && c.tag.front() == '[' && c.tag.back() == ']';
            const std::string header = hasBrackets ? c.tag : ("[" + c.tag + "]");
            game::log::infoTerminalOnly(&services_.log, header + " " + c.payload);
        } else {
            game::log::info(&services_.log, c.tag);
        }
        return;
    }

    if (std::holds_alternative<SpawnCommand>(cmd)) {
        const auto& c = std::get<SpawnCommand>(cmd);
        if (world_) world_->spawnPokemon(c.name, {c.x, c.y, c.z});
        return;
    }

    if (std::holds_alternative<SpawnOnGridCommand>(cmd)) {
        const auto& c = std::get<SpawnOnGridCommand>(cmd);
        if (world_) world_->spawnPokemonAtGrid(c.name, c.col, c.row, c.side, c.level);
        return;
    }

    if (std::holds_alternative<PushStateCommand>(cmd)) {
        const auto& c = std::get<PushStateCommand>(cmd);
        if (manager_) {
            manager_->pushState(std::make_unique<ScriptedState>(manager_, world_, services_, c.scriptPath));
        }
        return;
    }

    if (std::holds_alternative<PushCombatStateCommand>(cmd)) {
        const auto& c = std::get<PushCombatStateCommand>(cmd);
        if (manager_) {
            manager_->pushState(std::make_unique<CombatState>(manager_, world_, services_, c.scriptPath));
        }
        return;
    }

    if (std::holds_alternative<PopStateCommand>(cmd)) {
        if (manager_) manager_->popState();
        return;
    }

    if (std::holds_alternative<AddToBenchCommand>(cmd)) {
        const auto& c = std::get<AddToBenchCommand>(cmd);
        if (world_) world_->addToBench(c.name, c.level);
        return;
    }

    if (std::holds_alternative<ApplyMoveCommand>(cmd)) {
        const auto& c = std::get<ApplyMoveCommand>(cmd);
        if (!world_) return;
        auto* u = world_->findUnitById(c.unitId);
        if (!u || !isCombatActive(*u)) return;
        u->position = world_->gridToWorld(c.col, c.row);
        u->isMoving = false;
        u->moveT = 1.0f;
        u->committedDest = {-1, -1};
        return;
    }

    if (std::holds_alternative<CommitMoveCommand>(cmd)) {
        const auto& c = std::get<CommitMoveCommand>(cmd);
        if (!world_) return;
        auto* u = world_->findUnitById(c.unitId);
        if (!u || !isCombatActive(*u)) return;
        const glm::ivec2 target{c.col, c.row};
        for (const auto& other : world_->getPokemons()) {
            if (!other.alive && !other.captureInProgress && !(other.fainting && config().faintBlockTiles)) continue;
            if (other.id == u->id) continue;

            const auto oc = world_->worldToGrid(other.position);
            if (oc == target) return;
            if (other.committedDest.x >= 0 && other.committedDest.y >= 0) {
                if (other.committedDest == target) return;
            }
        }
        u->committedDest = {c.col, c.row};
        u->moveFrom = u->position;
        u->moveTo = world_->gridToWorld(c.col, c.row);
        u->moveT = 0.0f;
        u->isMoving = true;
        return;
    }

    if (std::holds_alternative<FaceEnemyCommand>(cmd)) {
        const auto& c = std::get<FaceEnemyCommand>(cmd);
        if (!world_) return;
        auto& list = world_->getPokemons();
        auto it = std::find_if(list.begin(), list.end(),
            [&](const PokemonInstance& p) { return p.id == c.unitId; });
        if (it == list.end()) return;

        glm::vec3 target;
        if (c.hasTarget) {
            target = world_->gridToWorld(c.col, c.row);
        } else {
            float best = std::numeric_limits<float>::max();
            glm::vec3 bestPos = it->position;
            for (auto& u : list) {
                if (!isCombatActive(u) || u.side == it->side) continue;
                const float d = glm::distance(it->position, u.position);
                if (d < best) {
                    best = d;
                    bestPos = u.position;
                }
            }
            target = bestPos;
        }
        setFacingToTarget(*it, target);
        return;
    }

    if (std::holds_alternative<FaceTargetCommand>(cmd)) {
        const auto& c = std::get<FaceTargetCommand>(cmd);
        if (!world_) return;
        if (c.unitId < 0 || c.targetId < 0) return;

        auto* u = world_->findUnitById(c.unitId);
        auto* t = world_->findUnitById(c.targetId);
        if (!u || !t) return;

        setFacingToTarget(*u, t->position);
        return;
    }

    if (std::holds_alternative<SetEnergyCommand>(cmd)) {
        const auto& c = std::get<SetEnergyCommand>(cmd);
        if (!world_) return;
        auto* u = world_->findUnitById(c.unitId);
        if (!u) return;
        u->energy = std::max(0, std::min(c.value, u->maxEnergy));
        return;
    }

    if (std::holds_alternative<AddEnergyCommand>(cmd)) {
        const auto& c = std::get<AddEnergyCommand>(cmd);
        if (!world_) return;
        auto* u = world_->findUnitById(c.unitId);
        if (!u) return;
        const int m = u->maxEnergy;
        u->energy = std::max(0, std::min(u->energy + c.delta, m));
        return;
    }

    if (std::holds_alternative<StartNewGameCommand>(cmd)) {
        const auto& c = std::get<StartNewGameCommand>(cmd);
        const std::string mode = toLowerCopy(c.mode);
        if (mode == "classic" || mode == "adventure") {
            services_.gameMode = mode;
        }
        services_.hasStartedGame = true;

        if (world_) {
            int startingMoney = services_.config.startingCash;
            if (services_.gameMode == "classic") {
                startingMoney = services_.config.classicStartingGold;
            }
            world_->resetForNewGame(startingMoney);
        }

        if (manager_) {
            manager_->clearAndPushState(std::make_unique<ScriptedState>(
                manager_, world_, services_, "scripts/states/starter.lua"));
        }
        return;
    }
}
