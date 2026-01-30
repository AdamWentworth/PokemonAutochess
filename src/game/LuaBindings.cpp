// src/game/LuaBindings.cpp
#include "LuaBindings.h"
#include "GameWorld.h"
#include "PokemonInstance.h"
#include "FlightLocomotion.h"
#include "GameStateManager.h"
#include "ScriptedState.h"
#include "engine/events/EventManager.h"
#include "engine/events/RoundEvents.h"
#include "GameConfig.h"
#include "PokemonConfigLoader.h"
#include "MovesConfigLoader.h"
#include "AttackAnimConfigLoader.h"
#include "AttackAnimDebug.h"
#include "AnimSetLoader.h"
#include <glm/glm.hpp>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include "LogBus.h"


static std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

static int animIndexCached(PokemonInstance& p, const std::string& clipName) {
    if (!p.model) return -1;
    if (clipName.empty()) return -1;

    auto it = p.animIndexCache.find(clipName);
    if (it != p.animIndexCache.end()) return it->second;

    const int idx = AnimSet::resolveAnimIndex(p.model.get(), clipName);
    p.animIndexCache[clipName] = idx;
    return idx;
}
// Helper
static PokemonSide sideFromString(const std::string& s) {
    if (s == "Enemy" || s == "enemy") return PokemonSide::Enemy;
    return PokemonSide::Player;
}

// Grid helpers
static glm::vec3 gridToWorld(int col, int row) {
    const auto& cfg = GameConfig::get();
    float boardOriginX = -((cfg.cols * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    float boardOriginZ = -((cfg.rows * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    return { boardOriginX + col * cfg.cellSize, 0.0f, boardOriginZ + row * cfg.cellSize };
}
static glm::ivec2 worldToGrid(const glm::vec3& pos) {
    const auto& cfg = GameConfig::get();
    float boardOriginX = -((cfg.cols * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    float boardOriginZ = -((cfg.rows * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    int col = static_cast<int>(std::round((pos.x - boardOriginX) / cfg.cellSize));
    int row = static_cast<int>(std::round((pos.z - boardOriginZ) / cfg.cellSize));
    return { col, row };
}

// ============================================================================
// IMPORTANT GAMEPLAY CHANGE (outgoing damage gating):
//
// - Receiving damage: unchanged (targets always lose HP when this function decides to apply it).
// - Applying damage: ONLY occurs if the attacker is actually playing its attack animation
//   (i.e., attackTimerSec > 0 and activeAnimIndex == animAttack1Index).
//
// This prevents "ghost" hits during takeoff/landing/other cosmetic animations.
//
// NOTE: This assumes your combat loop calls world_apply_damage at the time it *wants* to
// deal damage. If the loop calls it continuously every tick while in range, this gating
// will make it apply damage only once you start the attack animation (and only while it’s active),
// but you should still make sure the combat logic has a cooldown / one-shot trigger.
// ============================================================================

static bool attackerIsInAttackAnimation(const PokemonInstance& A) {
    if (!A.alive) return false;
    if (A.attackTimerSec <= 0.0f) return false;

    // Use the currently-selected attack clip (start/loop/end/one_shot), not hard-coded attack1.
    const int idx = (A.currentAttackAnimIndex >= 0) ? A.currentAttackAnimIndex : A.animAttack1Index;
    if (idx < 0) return false;

    return (A.activeAnimIndex == idx);
}

void registerLuaBindings(sol::state& lua, GameWorld* world, GameStateManager* manager) {
    // Basic enums
    lua.new_enum("PokemonSide",
        "Player", PokemonSide::Player,
        "Enemy",  PokemonSide::Enemy
    );

    // ---- Logging: Lua -> BattleFeed ----
    lua.set_function("emit", [](const std::string& tag_or_msg, sol::optional<std::string> payload) {
        if (payload.has_value() && !payload->empty()) {
            // Structured (verbose) log -> terminal only
            const std::string& tag = tag_or_msg;
            const bool hasBrackets = !tag.empty() && tag.front()=='[' && tag.back()==']';
            const std::string header = hasBrackets ? tag : ("[" + tag + "]");
            LogBus::infoTerminalOnly(header + " " + *payload);
        } else {
            // Human-readable line -> show in on-screen feed (and mirror to terminal)
            LogBus::info(tag_or_msg);
        }
    });

    // ---- Engine-safe spawners ----
    lua.set_function("spawnPokemon", [world](std::string name, float x, float y, float z) {
        if (world) world->spawnPokemon(name, {x, y, z});
    });
    lua.set_function("spawn_on_grid",
    [world](std::string name, int col, int row, std::string side, sol::optional<int> level) {
        int lvl = level.value_or(-1);
        if (world) world->spawnPokemonAtGrid(name, col, row, sideFromString(side), lvl);
    });

    // ---- Round events ----
    lua.set_function("emit_round_phase_changed",
        [](const std::string& prev, const std::string& next) {
            RoundPhaseChangedEvent evt(prev, next);
            EventManager::getInstance().emit(evt);
        }
    );

    // ---- State mgmt ----
    lua.set_function("push_state", [manager, world](const std::string& scriptPath) {
        if (!manager) return;
        manager->pushState(std::make_unique<ScriptedState>(manager, world, scriptPath));
    });
    lua.set_function("pop_state", [manager]() { if (manager) manager->popState(); });

    // =================================================================
    // World/Unit inspection & mutation for Lua systems
    // =================================================================
    lua.set_function("world_list_units", [world, &lua]() {
        sol::state_view L(lua);
        sol::table arr = L.create_table();
        if (!world) return arr;
        int i = 1;
        for (auto& u : world->getPokemons()) {
            sol::table t = L.create_table();
            t["id"]        = u.id;
            t["name"]      = u.name;
            t["side"]      = (u.side == PokemonSide::Player) ? "Player" : "Enemy";
            t["hp"]        = u.hp;
            t["attack"]    = u.attack;
            t["speed"]     = u.movementSpeed;
            t["energy"]    = u.energy;
            t["maxEnergy"] = u.maxEnergy;
            auto cell      = worldToGrid(u.position);
            t["col"]       = cell.x;
            t["row"]       = cell.y;
            t["alive"]     = u.alive;
            t["fastMove"]  = u.fastMove;
            t["chargedMove"] = u.chargedMove;
            arr[i++]       = t;
        }
        return arr;
    });

    lua.set_function("world_get_unit_snapshot", [world, &lua](int unitId) {
        sol::state_view L(lua);
        sol::table t = L.create_table();
        if (!world) return t;
        for (auto& u : world->getPokemons()) {
            if (u.id == unitId) {
                t["id"]        = u.id;
                t["name"]      = u.name;
                t["side"]      = (u.side == PokemonSide::Player) ? "Player" : "Enemy";
                t["hp"]        = u.hp;
                t["attack"]    = u.attack;
                t["alive"]     = u.alive;
                t["energy"]    = u.energy;
                t["maxEnergy"] = u.maxEnergy;
                t["fastMove"]  = u.fastMove;
                t["chargedMove"] = u.chargedMove;
                auto cell      = worldToGrid(u.position);
                t["col"]       = cell.x;
                t["row"]       = cell.y;
                return t;
            }
        }
        return t;
    });

    // Movement & adjacency helpers (unchanged)
    lua.set_function("world_apply_move", [world](int unitId, int col, int row) {
        if (!world) return false;
        auto& list = world->getPokemons();
        auto it = std::find_if(list.begin(), list.end(),
            [&](const PokemonInstance& p){ return p.id == unitId; });
        if (it == list.end() || !it->alive) return false;
        it->position = gridToWorld(col, row);
        it->isMoving = false;
        it->moveT = 1.0f;
        it->committedDest = {-1,-1};
        return true;
    });

    lua.set_function("world_commit_move", [world](int unitId, int col, int row) {
        if (!world) return false;
        auto& list = world->getPokemons();
        auto it = std::find_if(list.begin(), list.end(),
            [&](const PokemonInstance& p){ return p.id == unitId; });
        if (it == list.end() || !it->alive) return false;

        // If this unit is in a chained fast-attack loop, optionally play the configured "end" clip
        // as a short transition cue before moving.
        if (it->fastChainTimerSec > 0.0f && !it->chainedFastMove.empty()) {
            const std::string speciesLower = toLowerCopy(it->name);
            const std::string clipEnd = AttackAnimConfigLoader::getInstance().getClipName(
                speciesLower, "fast", it->chainedFastMove, "end");

            const int endIdx = animIndexCached(*it, clipEnd);
            if (endIdx >= 0 && it->model) {
                const float clipDur = it->model->getAnimationDurationSec(endIdx);
                const float window  = 0.25f; // short cue; don't stall movement timing

                it->attackTimerSec = window;
                it->attackAnimSpeed = (clipDur > 0.0f) ? (clipDur / window) : 1.0f;

                it->currentAttackAnimIndex = endIdx;
                it->activeAnimIndex = endIdx;
                it->animTimeSec = 0.0f;
            }

            it->chainedFastMove.clear();
            it->fastChainTimerSec = 0.0f;
        }

        const auto target = gridToWorld(col,row);
        it->committedDest = {col,row};
        it->moveFrom      = it->position;
        it->moveTo        = target;
        it->moveT         = 0.0f;
        it->isMoving      = true;
        return true;
    });

    lua.set_function("world_nearest_enemy_cell", [world](int unitId) {
        if (!world) return std::make_pair(-1, -1);

        auto& list = world->getPokemons();

        // Find the querying unit
        const auto it = std::find_if(list.begin(), list.end(),
            [&](const PokemonInstance& p){ return p.id == unitId; });
        if (it == list.end()) return std::make_pair(-1, -1);

        const auto myCell = worldToGrid(it->position);

        int best = std::numeric_limits<int>::max();
        glm::ivec2 bestCell(-1, -1);

        for (const auto& u : list) {
            if (!u.alive || u.side == it->side) continue;
            const auto ec = worldToGrid(u.position);

            // Chebyshev distance matches your 8-connected neighborhood
            const int d = std::max(std::abs(myCell.x - ec.x), std::abs(myCell.y - ec.y));
            if (d < best) {
                best = d;
                bestCell = ec;
            }
        }

        return std::make_pair(bestCell.x, bestCell.y);
    });

    // FIX: use integer distances to avoid int->float C4244 warnings
    lua.set_function("world_is_adjacent_to_enemy", [world](int unitId) {
        if (!world) return false;
        auto& list = world->getPokemons();
        auto it = std::find_if(list.begin(), list.end(),
            [&](const PokemonInstance& p){ return p.id == unitId; });
        if (it == list.end()) return false;
        auto myCell = worldToGrid(it->position);

        int best = std::numeric_limits<int>::max();
        glm::ivec2 bestCell(-999,-999);
        for (auto& u : list) {
            if (!u.alive || u.side == it->side) continue;
            auto ec = worldToGrid(u.position);
            const int d = std::max(std::abs(myCell.x - ec.x), std::abs(myCell.y - ec.y));
            if (d < best) { best = d; bestCell = ec; }
        }
        const int dx = std::abs(myCell.x - bestCell.x);
        const int dy = std::abs(myCell.y - bestCell.y);
        return std::max(dx, dy) == 1;
    });

    lua.set_function("world_enemies_adjacent", [world, &lua](int unitId) {
        sol::state_view L(lua);
        sol::table arr = L.create_table();
        if (!world) return arr;

        PokemonInstance* attacker = nullptr;
        for (auto& u : world->getPokemons()) if (u.id == unitId) { attacker = &u; break; }
        if (!attacker || !attacker->alive) return arr;

        auto ac = worldToGrid(attacker->position);
        int idx = 1;
        for (auto& u : world->getPokemons()) {
            if (!u.alive || u.side == attacker->side) continue;
            auto ec = worldToGrid(u.position);
            const int dx = std::abs(ac.x - ec.x);
            const int dy = std::abs(ac.y - ec.y);
            if (std::max(dx, dy) == 1) {
                arr[idx++] = u.id;
            }
        }
        return arr;
    });


    // Can this unit currently *initiate* an attack?
    // - non-fliers: true (if alive and not moving)
    // - fliers using visual-only flight: only true when grounded (prevents "ghost" hits mid takeoff/landing)
    lua.set_function("world_can_attack", [world](int unitId) {
        if (!world) return false;
        for (auto& u : world->getPokemons()) {
            if (u.id != unitId) continue;
            if (!u.alive) return false;
            if (u.isMoving) return false;
            if (u.usesAirLocomotion && FlightLocomotion::isAirborne(u)) return false;
            return true;
        }
        return false;
    });

    lua.set_function("world_apply_damage",
        [world](int attackerId,
                int targetId,
                int amount,
                sol::optional<float> cadenceSec,
                sol::optional<std::string> moveName,
                sol::optional<std::string> kind) {
        if (!world) return -1;

        auto& list = world->getPokemons();

        auto A = std::find_if(list.begin(), list.end(),
            [&](const PokemonInstance& p){ return p.id == attackerId; });
        auto T = std::find_if(list.begin(), list.end(),
            [&](const PokemonInstance& p){ return p.id == targetId; });

        if (A == list.end() || T == list.end()) return -1;

        const std::string speciesLower = toLowerCopy(A->name);
        const std::string moveLower    = moveName ? toLowerCopy(*moveName) : "";
        std::string kindLower          = kind ? toLowerCopy(*kind) : "";

        if (kindLower.empty() && !moveLower.empty()) {
            if (const MoveData* md = MovesConfigLoader::getInstance().getMove(moveLower)) {
                kindLower = toLowerCopy(md->kind);
            }
        }
        if (kindLower.empty()) kindLower = "fast";

        // If this attacker has an attack animation, enforce: 1 damage event == 1 animation cycle.
        if (A->attackDurationSec > 0.0f && A->animAttack1Index >= 0) {
            bool airborne = false;
            if (A->usesAirLocomotion) airborne = FlightLocomotion::isAirborne(*A);

            float desiredWindowSec = cadenceSec.value_or(0.0f);
            if (desiredWindowSec <= 0.0f) desiredWindowSec = A->attackDurationSec;
            if (desiredWindowSec <= 0.0f && A->model) {
                desiredWindowSec = A->model->getAnimationDurationSec(A->animAttack1Index);
            }

            // Pick a clip from config (if any).
            const auto& animCfg = AttackAnimConfigLoader::getInstance();

            int desiredAnimIdx = A->animAttack1Index;

            if (!speciesLower.empty()) {
                if (kindLower == "charged") {
                    const std::string clip = animCfg.getClipName(speciesLower, "charged", moveLower, "one_shot");
                    const int idx = animIndexCached(*A, clip);
                    if (idx >= 0) desiredAnimIdx = idx;
                } else if (kindLower == "fast" && !moveLower.empty()) {
                    const std::string clipStart = animCfg.getClipName(speciesLower, "fast", moveLower, "start");
                    const std::string clipLoop  = animCfg.getClipName(speciesLower, "fast", moveLower, "loop");
                    const std::string clipEnd   = animCfg.getClipName(speciesLower, "fast", moveLower, "end");
                    const std::string clipDef   = animCfg.getClipName(speciesLower, "fast", moveLower, "default");

                    const bool hasStart = !clipStart.empty();
                    const bool hasLoop  = !clipLoop.empty();
                    const bool hasEnd   = !clipEnd.empty();

                    const int hpBefore = T->hp;
                    const int dmg      = std::max(0, amount);
                    const bool willKill = (dmg > 0 && (hpBefore - dmg) <= 0);

                    if (willKill && hasEnd) {
                        const int idx = animIndexCached(*A, clipEnd);
                        if (idx >= 0) desiredAnimIdx = idx;
                        A->chainedFastMove.clear();
                        A->fastChainTimerSec = 0.0f;
                    } else if (A->fastChainTimerSec > 0.0f && A->chainedFastMove == moveLower && hasLoop) {
                        const int idx = animIndexCached(*A, clipLoop);
                        if (idx >= 0) desiredAnimIdx = idx;
                    } else if (hasStart) {
                        const int idx = animIndexCached(*A, clipStart);
                        if (idx >= 0) desiredAnimIdx = idx;
                    } else if (hasLoop) {
                        const int idx = animIndexCached(*A, clipLoop);
                        if (idx >= 0) desiredAnimIdx = idx;
                    } else if (!clipDef.empty()) {
                        const int idx = animIndexCached(*A, clipDef);
                        if (idx >= 0) desiredAnimIdx = idx;
                    }

                    // Extend/refresh chain window so repeated fast attacks can switch to "loop".
                    if (dmg > 0) {
                        A->chainedFastMove = moveLower;
                        const float cd = std::max(0.05f, desiredWindowSec);
                        A->fastChainTimerSec = std::max(A->fastChainTimerSec, cd * 1.25f);
                    }
                }
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
                // Queue a single attack cycle that will start once we finish landing.
                FlightLocomotion::queueAttackAfterLanding(*A, desiredWindowSec, desiredAnimIdx);
                return T->hp;
            }

            bool startedThisCall = false;
            if (A->attackTimerSec <= 0.0f || A->activeAnimIndex != desiredAnimIdx) {
                const float clipDur  = (A->model ? A->model->getAnimationDurationSec(desiredAnimIdx) : A->attackDurationSec);
                const float windowSec = (desiredWindowSec > 0.0f ? desiredWindowSec : clipDur);

                A->attackTimerSec = windowSec;
                A->animTimeSec = 0.0f;
                A->currentAttackAnimIndex = desiredAnimIdx;
                A->activeAnimIndex = desiredAnimIdx;
                A->attackAnimSpeed = (windowSec > 0.0f && clipDur > 0.0f) ? (clipDur / windowSec) : 1.0f;

                startedThisCall = true;
            }

            std::string phase = "default";
            std::string clipUsed;
            if (kindLower == "charged") {
                phase = "one_shot";
                clipUsed = AttackAnimConfigLoader::getInstance().getClipName(speciesLower, "charged", moveLower, "one_shot");
            } else if (kindLower == "fast") {
                const int hpBefore = T->hp;
                const int dmg      = std::max(0, amount);
                const bool willKill = (dmg > 0 && (hpBefore - dmg) <= 0);

                if (willKill) { phase = "end"; clipUsed = AttackAnimConfigLoader::getInstance().getClipName(speciesLower, "fast", moveLower, "end"); }
                else if (A->fastChainTimerSec > 0.0f && A->chainedFastMove == moveLower) { phase = "loop"; clipUsed = AttackAnimConfigLoader::getInstance().getClipName(speciesLower, "fast", moveLower, "loop"); }
                else { phase = "start"; clipUsed = AttackAnimConfigLoader::getInstance().getClipName(speciesLower, "fast", moveLower, "start"); }
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

            // Only allow damage once per attack cycle.
            if (!startedThisCall || amount <= 0) return T->hp;

            if (!attackerIsInAttackAnimation(*A)) return T->hp;
        }

        // Apply damage.
        int dmg = std::max(0, amount);
        T->hp = std::max(0, T->hp - dmg);

        if (T->hp <= 0) {
            T->hp = 0;
            T->alive = false;

            // optional cleanup so dead units don't keep doing leftover animation state
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
    });

    lua.set_function("world_face_enemy", [world](int unitId, sol::optional<int> tgtCol, sol::optional<int> tgtRow) {
        if (!world) return;
        auto& list = world->getPokemons();
        auto it = std::find_if(list.begin(), list.end(),
            [&](const PokemonInstance& p){ return p.id == unitId; });
        if (it == list.end()) return;

        glm::vec3 target;
        if (tgtCol && tgtRow) {
            target = gridToWorld(*tgtCol, *tgtRow);
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
    });

    // Grid converters
    lua.set_function("grid_to_world", [](int col, int row) {
        auto p = gridToWorld(col, row);
        return std::make_tuple(p.x, p.y, p.z);
    });
    lua.set_function("world_to_grid", [](float x, float y, float z) {
        auto c = worldToGrid(glm::vec3{x,y,z});
        return std::make_pair(c.x, c.y);
    });

    // ----- Energy helpers -----
    lua.set_function("world_get_energy", [world](int unitId) {
        if (!world) return 0;
        for (auto& u : world->getPokemons()) if (u.id == unitId) return u.energy;
        return 0;
    });
    lua.set_function("world_get_max_energy", [world](int unitId) {
        if (!world) return 100;
        for (auto& u : world->getPokemons()) if (u.id == unitId) return u.maxEnergy;
        return 100;
    });
    lua.set_function("world_set_energy", [world](int unitId, int value) {
        if (!world) return false;
        for (auto& u : world->getPokemons()) if (u.id == unitId) {
            u.energy = std::max(0, std::min(value, u.maxEnergy));
            return true;
        }
        return false;
    });
    lua.set_function("world_add_energy", [world](int unitId, int delta) {
        if (!world) return 0;
        for (auto& u : world->getPokemons()) if (u.id == unitId) {
            int m = u.maxEnergy;
            u.energy = std::max(0, std::min(u.energy + delta, m));
            return u.energy;
        }
        return 0;
    });

    // ====== move accessors for Lua combat ======
    lua.set_function("unit_fast_move", [world](int unitId) -> std::string {
        if (!world) return "";
        for (auto& u : world->getPokemons()) if (u.id == unitId) return u.fastMove;
        return "";
    });
    lua.set_function("unit_charged_move", [world](int unitId) -> std::string {
        if (!world) return "";
        for (auto& u : world->getPokemons()) if (u.id == unitId) return u.chargedMove;
        return "";
    });
    lua.set_function("move_get", [&lua](const std::string& name) {
        sol::state_view L(lua);
        sol::table t = L.create_table();
        const auto* md = MovesConfigLoader::getInstance().getMove(name);
        if (!md) return t;
        t["name"]        = md->name;
        t["type"]        = md->type;
        t["kind"]        = md->kind;
        t["cooldownSec"] = md->cooldownSec;
        t["power"]       = md->power;
        t["range"]       = md->range;
        t["energyGain"]  = md->energyGain;
        t["energyCost"]  = md->energyCost;
        if (md->status.valid) {
            sol::table s = L.create_table();
            s["effect"]      = md->status.effect;
            s["magnitude"]   = md->status.magnitude;
            s["durationSec"] = md->status.durationSec;
            s["target"]      = md->status.target;
            t["status"] = s;
        }
        return t;
    });
}

