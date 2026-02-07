// src/game/scripting/ScriptAPI.cpp

#include "game/scripting/ScriptAPI.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "engine/render/Model.h"

#include "game/GameWorld.h"
#include "game/GameStateManager.h"
#include "game/state/ScriptedState.h"
#include "game/logging/LoggerUtil.h"

#include "game/animation/FlightLocomotion.h"
#include "game/animation/AttackAnimDebug.h"

#include "game/config/GameDataDb.h"
#include "game/config/MovesConfigLoader.h"
#include "game/config/AttackAnimConfigLoader.h"
#include "game/config/AnimSetLoader.h"

#include "game/logging/DebugTrace.h"

#include "LuaBindings_Internal.h"

ScriptAPI::ScriptAPI(GameWorld* world, GameStateManager* manager, LogBus::Logger* logger, ScriptEventBus* events)
    : world_(world), manager_(manager), logger_(logger), events_(events) {}

const GameConfigData* ScriptAPI::config() const {
    return world_ ? world_->getConfig() : nullptr;
}

std::vector<ScriptEvent> ScriptAPI::drainEvents() {
    if (!events_) return {};
    return events_->drain();
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

void ScriptAPI::pushState(const std::string& scriptPath) {
    PushStateCommand cmd;
    cmd.scriptPath = scriptPath;
    enqueue(cmd);
}

void ScriptAPI::popState() {
    enqueue(PopStateCommand{});
}

bool ScriptAPI::applyMove(int unitId, int col, int row) {
    if (!world_) return false;
    auto* u = world_->findUnitById(unitId);
    if (!u || !u->alive) return false;

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
    if (!u || !u->alive) return false;

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
    int m = u->maxEnergy;
    return std::max(0, std::min(u->energy + delta, m));
}

void ScriptAPI::applyCommand(const Command& cmd) {
    if (std::holds_alternative<EmitCommand>(cmd)) {
        const auto& c = std::get<EmitCommand>(cmd);
        if (events_) {
            if (c.hasPayload) {
                events_->emit(c.tag, c.payload);
            } else {
                events_->emit("log", c.tag);
            }
        }
        if (c.hasPayload) {
            const bool hasBrackets = !c.tag.empty() && c.tag.front()=='[' && c.tag.back()==']';
            const std::string header = hasBrackets ? c.tag : ("[" + c.tag + "]");
            game::log::infoTerminalOnly(logger_, header + " " + c.payload);
        } else {
            game::log::info(logger_, c.tag);
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
            manager_->pushState(std::make_unique<ScriptedState>(manager_, world_, c.scriptPath));
        }
        return;
    }

    if (std::holds_alternative<PopStateCommand>(cmd)) {
        if (manager_) manager_->popState();
        return;
    }

    if (std::holds_alternative<ApplyMoveCommand>(cmd)) {
        const auto& c = std::get<ApplyMoveCommand>(cmd);
        if (!world_) return;
        auto* u = world_->findUnitById(c.unitId);
        if (!u || !u->alive) return;
        u->position = gridToWorld(config(), c.col, c.row);
        u->isMoving = false;
        u->moveT = 1.0f;
        u->committedDest = {-1,-1};
        return;
    }

    if (std::holds_alternative<CommitMoveCommand>(cmd)) {
        const auto& c = std::get<CommitMoveCommand>(cmd);
        if (!world_) return;
        auto* u = world_->findUnitById(c.unitId);
        if (!u || !u->alive) return;
        u->committedDest = {c.col, c.row};
        u->moveFrom = u->position;
        u->moveTo = gridToWorld(config(), c.col, c.row);
        u->moveT = 0.0f;
        u->isMoving = true;
        return;
    }

    if (std::holds_alternative<FaceEnemyCommand>(cmd)) {
        const auto& c = std::get<FaceEnemyCommand>(cmd);
        if (!world_) return;
        auto& list = world_->getPokemons();
        auto it = std::find_if(list.begin(), list.end(),
            [&](const PokemonInstance& p){ return p.id == c.unitId; });
        if (it == list.end()) return;

        glm::vec3 target;
        if (c.hasTarget) {
            target = gridToWorld(config(), c.col, c.row);
        } else {
            float best = std::numeric_limits<float>::max();
            glm::vec3 bestPos = it->position;
            for (auto& u : list) {
                if (!u.alive || u.side == it->side) continue;
                float d = glm::distance(it->position, u.position);
                if (d < best) { best = d; bestPos = u.position; }
            }
            target = bestPos;
        }
        glm::vec3 lookDir = glm::normalize(target - it->position);
        it->rotation.y = std::atan2(lookDir.x, lookDir.z) * 180.0f / 3.14159265358979323846f;
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
        int m = u->maxEnergy;
        u->energy = std::max(0, std::min(u->energy + c.delta, m));
        return;
    }
}

int ScriptAPI::applyDamage(int attackerId,
                           int targetId,
                           int amount,
                           const std::optional<float>& cadenceSec,
                           const std::optional<std::string>& moveName,
                           const std::optional<std::string>& kind) {
    if (!world_) return -1;

    const auto* data = world_->getData();
    auto& list = world_->getPokemons();

    auto A = std::find_if(list.begin(), list.end(),
        [&](const PokemonInstance& p){ return p.id == attackerId; });
    auto T = std::find_if(list.begin(), list.end(),
        [&](const PokemonInstance& p){ return p.id == targetId; });

    if (A == list.end() || T == list.end()) return -1;

    const std::string speciesLower = toLowerCopy(A->name);
    const std::string moveLower    = moveName ? toLowerCopy(*moveName) : "";
    std::string kindLower          = kind ? toLowerCopy(*kind) : "";

    if (kindLower.empty() && !moveLower.empty()) {
        if (data) {
            if (const MoveData* md = data->moves.getMove(moveLower)) {
                kindLower = toLowerCopy(md->kind);
            }
        }
    }
    if (kindLower.empty()) kindLower = "fast";

    const bool traceCombat = DebugTrace::combat(speciesLower, moveLower);
    auto trlog = [&](const std::string& msg) {
        if (!traceCombat) return;
        game::log::infoTerminalOnly(logger_, std::string("[TRACE_COMBAT_CPP] ") +
                                 "unit=" + speciesLower + " move=" + (moveLower.empty() ? std::string("-") : moveLower) + " " + msg);
    };

    if (traceCombat) {
        trlog(std::string("enter attackerId=") + std::to_string(attackerId) +
              " targetId=" + std::to_string(targetId) +
              " kind=" + kindLower +
              " move=" + (moveLower.empty() ? std::string("-") : moveLower) +
              " amount=" + std::to_string(amount) +
              " cadenceSec_in=" + std::to_string(cadenceSec.value_or(-1.0f)) +
              " atkTimer=" + std::to_string(A->attackTimerSec) +
              " atkDur=" + std::to_string(A->attackDurationSec) +
              " activeAnimIdx=" + std::to_string(A->activeAnimIndex) +
              " curAtkAnimIdx=" + std::to_string(A->currentAttackAnimIndex) +
              " atkAnimSpeed=" + std::to_string(A->attackAnimSpeed) +
              " fastChainTimerSec=" + std::to_string(A->fastChainTimerSec) +
              " chainedFastMove=" + (A->chainedFastMove.empty() ? std::string("-") : A->chainedFastMove));
    }

    if (A->attackDurationSec > 0.0f && A->animAttack1Index >= 0) {
        bool airborne = false;
        if (A->usesAirLocomotion) airborne = FlightLocomotion::isAirborne(*A);

        float desiredWindowSec = cadenceSec.value_or(0.0f);
        if (desiredWindowSec <= 0.0f) desiredWindowSec = A->attackDurationSec;

        const auto* animCfg = data ? &data->attackAnims : nullptr;

        const float minReqSec = animCfg
            ? animCfg->getMinRequestSec(speciesLower, kindLower, moveLower, logger_)
            : 0.0f;
        if (minReqSec > 0.0f) desiredWindowSec = std::max(desiredWindowSec, minReqSec);

        if (traceCombat) {
            const float baseCad = cadenceSec.value_or(0.0f);
            const float atk1Dur = (A->model && A->animAttack1Index >= 0) ? A->model->getAnimationDurationSec(A->animAttack1Index) : 0.0f;
            trlog(std::string("cadence baseCadenceArg=") + std::to_string(baseCad) +
                  " attackDurationSec=" + std::to_string(A->attackDurationSec) +
                  " clipDur_attack1=" + std::to_string(atk1Dur) +
                  " minReqSec=" + std::to_string(minReqSec) +
                  " desiredWindowSec=" + std::to_string(desiredWindowSec));
        }

        if (amount <= 0) {
            if (traceCombat) trlog("cosmetic: amount<=0 -> ignore (no cycle)");
            return T->hp;
        }

        const float kMidCycleEps = 0.0001f;
        if (A->attackTimerSec > kMidCycleEps) {
            if (traceCombat) trlog("lock: mid-cycle -> ignore request (no new cycle, no damage)");
            return T->hp;
        }

        int desiredAnimIdx = A->animAttack1Index;
        std::string phase = "default";
        std::string clipUsed;

        if (!speciesLower.empty()) {
            if (kindLower == "charged") {
                phase = "one_shot";
                clipUsed = animCfg
                    ? animCfg->getClipName(speciesLower, "charged", moveLower, "one_shot", logger_)
                    : std::string();
                const int idx = animIndexCached(*A, clipUsed);
                if (idx >= 0) desiredAnimIdx = idx;
            } else if (kindLower == "fast" && !moveLower.empty()) {
                const std::string clipLoop = animCfg
                    ? animCfg->getClipName(speciesLower, "fast", moveLower, "loop", logger_)
                    : std::string();
                const std::string clipDef  = animCfg
                    ? animCfg->getClipName(speciesLower, "fast", moveLower, "default", logger_)
                    : std::string();

                phase = "loop";
                clipUsed = clipLoop;
                if (clipUsed.empty()) {
                    phase = "default";
                    clipUsed = clipDef;
                }

                if (!clipUsed.empty()) {
                    const int idx = animIndexCached(*A, clipUsed);
                    if (idx >= 0) desiredAnimIdx = idx;
                }

                A->chainedFastMove.clear();
                A->fastChainTimerSec = 0.0f;
            }
        }

        if (traceCombat) {
            float clipDur = (A->model && desiredAnimIdx >= 0) ? A->model->getAnimationDurationSec(desiredAnimIdx) : 0.0f;
            trlog(std::string("resolved desiredAnimIdx=") + std::to_string(desiredAnimIdx) +
                  " clipDur=" + std::to_string(clipDur) +
                  " fastChainTimerSec=" + std::to_string(A->fastChainTimerSec) +
                  " chainedFastMove=" + (A->chainedFastMove.empty() ? std::string("-") : A->chainedFastMove));
        }

#ifdef PAC_DEBUG_ANIM
        std::cout << "[AnimDebug] " << A->name << " (ID " << A->id << ") "
                  << "attack requested airborne=" << (airborne ? "true" : "false")
                  << " dmg=" << amount
                  << " cadence=" << desiredWindowSec
                  << " animIdx=" << desiredAnimIdx
                  << " clipDur=" << (A->model ? A->model->getAnimationDurationSec(desiredAnimIdx) : 0.0f)
                  << "\n";
#endif

        if (airborne) {
            FlightLocomotion::queueAttackAfterLanding(*A, desiredWindowSec, desiredAnimIdx);
            return T->hp;
        }

        const float clipDur  = (A->model ? A->model->getAnimationDurationSec(desiredAnimIdx) : A->attackDurationSec);
        const float windowSec = std::max(0.05f, desiredWindowSec);

        A->attackTimerSec = windowSec;
        A->animTimeSec = 0.0f;
        A->currentAttackAnimIndex = desiredAnimIdx;
        A->activeAnimIndex = desiredAnimIdx;
        A->attackAnimSpeed = (windowSec > 0.0f && clipDur > 0.0f) ? (clipDur / windowSec) : 1.0f;

        A->pendingDamageActive = false;
        A->pendingDamageApplied = false;
        A->pendingDamageTargetId = -1;
        A->pendingDamageAmount = 0;
        A->pendingDamageHitTimeSec = 0.0f;

        const bool startedThisCall = true;

        if (traceCombat) {
            trlog(std::string("attack_state startedThisCall=true") +
                  " windowSec=" + std::to_string(windowSec) +
                  " clipDur=" + std::to_string(clipDur) +
                  " activeAnimIdx=" + std::to_string(A->activeAnimIndex) +
                  " currentAttackAnimIndex=" + std::to_string(A->currentAttackAnimIndex) +
                  " atkAnimSpeed=" + std::to_string(A->attackAnimSpeed) +
                  " atkTimer=" + std::to_string(A->attackTimerSec));
        }

        if (A->debugAnimLogs) {
            const int hpBeforeDbg = T->hp;
            const bool willKillDbg = (std::max(0, amount) > 0 && (hpBeforeDbg - std::max(0, amount) <= 0));
            const float clipDurDbg = (A->model && desiredAnimIdx >= 0) ? A->model->getAnimationDurationSec(desiredAnimIdx) : 0.0f;
            AttackAnimDebug::logSelection(*A, kindLower, moveLower, phase, clipUsed, desiredAnimIdx,
                                        clipDurDbg, desiredWindowSec, amount,
                                        hpBeforeDbg, (std::max(0, hpBeforeDbg - std::max(0, amount))), startedThisCall,
                                        willKillDbg, A->fastChainTimerSec);
        }

        if (amount <= 0) return T->hp;
        if (!attackerIsInAttackAnimation(*A)) return T->hp;

        const int hitFrame = animCfg ? animCfg->getHitFrame(speciesLower, kindLower, moveLower) : -1;
        if (hitFrame > 0) {
            if (!A->pendingDamageActive) {
                const float fps = (A->animFps > 0.0f) ? A->animFps : 24.0f;
                float hitTimeSec = (float)hitFrame / fps;

                const float clipDurClamp = (A->model && A->currentAttackAnimIndex >= 0)
                    ? A->model->getAnimationDurationSec(A->currentAttackAnimIndex)
                    : 0.0f;
                if (clipDurClamp > 0.0f) {
                    const float maxT = std::max(0.0f, clipDurClamp - 0.0001f);
                    hitTimeSec = std::min(hitTimeSec, maxT);
                }

                A->pendingDamageActive     = true;
                A->pendingDamageApplied    = false;
                A->pendingDamageTargetId   = targetId;
                A->pendingDamageAmount     = std::max(0, amount);
                A->pendingDamageHitTimeSec = std::max(0.0f, hitTimeSec);
            }

            return std::max(0, T->hp - std::max(0, amount));
        }
    }

    int dmg = std::max(0, amount);
    if (traceCombat) {
        trlog(std::string("damage_apply dmg=") + std::to_string(dmg) +
              " hp_before=" + std::to_string(T->hp));
    }
    T->hp = std::max(0, T->hp - dmg);
    if (traceCombat) {
        trlog(std::string("damage_result hp_after=") + std::to_string(T->hp) +
              " targetAlive=" + std::string(T->hp > 0 ? "true" : "false"));
    }

    if (T->hp <= 0) {
        T->hp = 0;
        T->alive = false;

        T->isMoving = false;
        T->attackTimerSec = 0.0f;
        T->attackAnimSpeed = 1.0f;
        T->currentAttackAnimIndex = T->animAttack1Index;
        T->pendingAttackAfterLanding = false;
        T->queuedAttackDurationSec = 0.0f;
        T->queuedAttackAnimIndex = -1;
        T->chainedFastMove.clear();
        T->fastChainTimerSec = 0.0f;
        T->animIndexCache.clear();
    }

    return T->hp;
}
