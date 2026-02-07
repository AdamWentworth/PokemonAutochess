// src/game/scripting/LuaBindings.cpp
#include <glm/glm.hpp>
#include "engine/render/Model.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

#include "LuaBindings.h"

#include "game/GameWorld.h"
#include "game/PokemonInstance.h"
#include "game/GameStateManager.h"
#include "game/GameConfig.h"

#include "game/animation/FlightLocomotion.h"
#include "game/animation/AttackAnimDebug.h"

#include "game/config/GameDataDb.h"
#include "game/config/MovesConfigLoader.h"
#include "game/config/AttackAnimConfigLoader.h"
#include "game/config/AnimSetLoader.h"

#include "game/state/ScriptedState.h"

#include "game/logging/LoggerUtil.h"
#include "game/logging/DebugTrace.h"

#include "LuaBindings_Internal.h"

void registerLuaBindings_World(sol::state& lua, GameWorld* world, GameStateManager* manager, LogBus::Logger* logger) {
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

        auto* u = world->findUnitById(unitId);
        if (!u) return t;

        t["id"]        = u->id;
        t["name"]      = u->name;
        t["side"]      = (u->side == PokemonSide::Player) ? "Player" : "Enemy";
        t["hp"]        = u->hp;
        t["attack"]    = u->attack;
        t["alive"]     = u->alive;
        t["energy"]    = u->energy;
        t["maxEnergy"] = u->maxEnergy;
        t["fastMove"]  = u->fastMove;
        t["chargedMove"] = u->chargedMove;
        auto cell      = worldToGrid(u->position);
        t["col"]       = cell.x;
        t["row"]       = cell.y;
        return t;
    });
// Movement & adjacency helpers (unchanged)

    lua.set_function("world_apply_move", [world](int unitId, int col, int row) {
        if (!world) return false;

        auto* u = world->findUnitById(unitId);
        if (!u || !u->alive) return false;

        u->position = gridToWorld(col, row);
        u->isMoving = false;
        u->moveT = 1.0f;
        u->committedDest = {-1,-1};
        return true;
    });

    lua.set_function("world_commit_move", [world](int unitId, int col, int row) {
        if (!world) return false;

        auto* u = world->findUnitById(unitId);
        if (!u || !u->alive) return false;

        // NOTE: Fast attacks no longer use __END transition cues when movement begins.
        u->committedDest = {col,row};
        u->moveFrom      = u->position;
        u->moveTo        = gridToWorld(col,row);
        u->moveT         = 0.0f;
        u->isMoving      = true;
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
            // IMPORTANT: do NOT gate attacks on isMoving.
            // Some movement systems keep isMoving=true while "holding" in melee,
            // which would suppress all attacks.
            // We only block initiating attacks for visual-only airborne locomotion.
            if (u.usesAirLocomotion && FlightLocomotion::isAirborne(u)) return false;
            return true;
        }
        return false;
    });

    // Can this unit *start a new attack animation cycle right now*?
    // - blocks when an attack is already playing (attackTimerSec > 0)
    // - blocks when visual-only flight locomotion is airborne
    lua.set_function("world_attack_ready", [world](int unitId) {
        if (!world) return false;
        for (auto& u : world->getPokemons()) {
            if (u.id != unitId) continue;
            if (!u.alive) return false;
            if (u.usesAirLocomotion && FlightLocomotion::isAirborne(u)) return false;
            if (u.attackTimerSec > 0.0001f) return false;
            return true;
        }
        return false;
    });

    lua.set_function("world_apply_damage",
        [world, logger](int attackerId,
                int targetId,
                int amount,
                sol::optional<float> cadenceSec,
                sol::optional<std::string> moveName,
                sol::optional<std::string> kind) {
        if (!world) return -1;

        const auto* data = world->getData();
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
            game::log::infoTerminalOnly(logger, std::string("[TRACE_COMBAT_CPP] ") +
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

        // If this attacker has an attack animation, enforce: 1 damage event == 1 animation cycle.
        if (A->attackDurationSec > 0.0f && A->animAttack1Index >= 0) {
            bool airborne = false;
            if (A->usesAirLocomotion) airborne = FlightLocomotion::isAirborne(*A);

            // Window = "how long this attack cycle should take" (cadence). We may speed up / slow down
            // the clip to fit the requested window. This keeps Lua cooldowns and animation windows aligned.
            float desiredWindowSec = cadenceSec.value_or(0.0f);
            if (desiredWindowSec <= 0.0f) desiredWindowSec = A->attackDurationSec;

            // Pick a clip from config (if any).
            const auto* animCfg = data ? &data->attackAnims : nullptr;

            // Per-move minimum request seconds (prevents unreasonably tiny windows).
            const float minReqSec = animCfg
                ? animCfg->getMinRequestSec(speciesLower, kindLower, moveLower, logger)
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

            // amount<=0 requests are cosmetic queries from Lua (used when attacks are blocked).
            // They must not start an animation window or block real damage later.
            if (amount <= 0) {
                if (traceCombat) trlog("cosmetic: amount<=0 -> ignore (no cycle)");
                return T->hp;
            }

            // If we're already mid-cycle, do NOT start a new cycle or apply damage.
            // Charged attacks are NOT allowed to preempt a running cycle; they should be
            // queued/triggered by Lua on the next available attack (after this one ends).
            const float kMidCycleEps = 0.0001f;
            if (A->attackTimerSec > kMidCycleEps) {
                if (traceCombat) trlog("lock: mid-cycle -> ignore request (no new cycle, no damage)");
                return T->hp;
            }

            // Choose phase/clip for THIS cycle (do not switch mid-cycle).
            int desiredAnimIdx = A->animAttack1Index;
            std::string phase = "default";
            std::string clipUsed;

            if (!speciesLower.empty()) {
                if (kindLower == "charged") {
                    phase = "one_shot";
                    clipUsed = animCfg
                        ? animCfg->getClipName(speciesLower, "charged", moveLower, "one_shot", logger)
                        : std::string();
                    const int idx = animIndexCached(*A, clipUsed);
                    if (idx >= 0) desiredAnimIdx = idx;
                } else if (kindLower == "fast" && !moveLower.empty()) {
                    const std::string clipLoop = animCfg
                        ? animCfg->getClipName(speciesLower, "fast", moveLower, "loop", logger)
                        : std::string();
                    const std::string clipDef  = animCfg
                        ? animCfg->getClipName(speciesLower, "fast", moveLower, "default", logger)
                        : std::string();

                    // New simplified policy:
                    // - NO __START
                    // - NO __END
                    // - Always use the configured loop clip (fallback to default).
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

                    // Disable legacy chain state (not used with loop-only behavior).
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
                // Queue a single attack cycle that will start once we finish landing.
                FlightLocomotion::queueAttackAfterLanding(*A, desiredWindowSec, desiredAnimIdx);
                return T->hp;
            }

            // Start this cycle.
            const float clipDur  = (A->model ? A->model->getAnimationDurationSec(desiredAnimIdx) : A->attackDurationSec);
            const float windowSec = std::max(0.05f, desiredWindowSec);

            A->attackTimerSec = windowSec;
            A->animTimeSec = 0.0f;
            A->currentAttackAnimIndex = desiredAnimIdx;
            A->activeAnimIndex = desiredAnimIdx;
            A->attackAnimSpeed = (windowSec > 0.0f && clipDur > 0.0f) ? (clipDur / windowSec) : 1.0f;


            // Reset any pending hit from a previous cycle.
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

            // Only allow damage once per attack cycle.
            if (amount <= 0) return T->hp;
            if (!attackerIsInAttackAnimation(*A)) return T->hp;

            // If this move has a configured hit frame, schedule damage to land during the animation.
            const int hitFrame = animCfg ? animCfg->getHitFrame(speciesLower, kindLower, moveLower) : -1;
            if (hitFrame > 0) {
                // Only schedule once per cycle.
                if (!A->pendingDamageActive) {
                    const float fps = (A->animFps > 0.0f) ? A->animFps : 24.0f;
                    float hitTimeSec = (float)hitFrame / fps;

                    // Clamp to clip duration if known (hit time is in *clip* seconds).
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

                // Return predicted HP (Lua often uses this for UI / state), but don't apply yet.
                return std::max(0, T->hp - std::max(0, amount));
            }
        }

// Apply damage.
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


    // Optional: per-move tuning for minimum seconds between attack requests.
    // Returns 0 when no override exists (scripts should fall back to their defaults).
    lua.set_function("world_attack_min_request_sec",
        [world, logger](int attackerId, sol::optional<std::string> moveName, sol::optional<std::string> kind) -> float {
            if (!world) return 0.0f;
            auto& list = world->getPokemons();
            auto A = std::find_if(list.begin(), list.end(),
                [&](const PokemonInstance& p){ return p.id == attackerId; });
            if (A == list.end()) return 0.0f;

            const std::string speciesLower = toLowerCopy(A->name);
            const std::string moveLower    = moveName ? toLowerCopy(*moveName) : "";
            std::string kindLower          = kind ? toLowerCopy(*kind) : "";

            const auto* data = world->getData();
            if (kindLower.empty() && !moveLower.empty()) {
                if (data) {
                    if (const MoveData* md = data->moves.getMove(moveLower)) {
                        kindLower = toLowerCopy(md->kind);
                    }
                }
            }
            if (kindLower.empty()) kindLower = "fast";

            return data
                ? data->attackAnims.getMinRequestSec(speciesLower, kindLower, moveLower, logger)
                : 0.0f;
        });

    // ----- Energy helpers -----

    lua.set_function("world_get_energy", [world](int unitId) {
        if (!world) return 0;
        if (auto* u = world->findUnitById(unitId)) return u->energy;
        return 0;
    });

    lua.set_function("world_get_max_energy", [world](int unitId) {
        if (!world) return 100;
        if (auto* u = world->findUnitById(unitId)) return u->maxEnergy;
        return 100;
    });

    lua.set_function("world_set_energy", [world](int unitId, int value) {
        if (!world) return false;
        auto* u = world->findUnitById(unitId);
        if (!u) return false;
        u->energy = std::max(0, std::min(value, u->maxEnergy));
        return true;
    });

    lua.set_function("world_add_energy", [world](int unitId, int delta) {
        if (!world) return 0;
        auto* u = world->findUnitById(unitId);
        if (!u) return 0;
        int m = u->maxEnergy;
        u->energy = std::max(0, std::min(u->energy + delta, m));
        return u->energy;
    });
// ====== move accessors for Lua combat ======

    
}
