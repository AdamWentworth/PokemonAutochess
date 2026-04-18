#include "game/world/GameWorld.h"

#include <chrono>
#include <iomanip>
#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_map>

#include "game/animation/FlightLocomotion.h"

#include "engine/core/EngineServices.h"
#include "engine/render/Model.h"

#include "game/logging/DebugTrace.h"
#include "game/logging/LoggerUtil.h"
#include "game/logging/ScratchPerfTrace.h"

namespace {

void clearPendingAttackState(PokemonInstance& unit) {
    unit.pendingProjectileActive = false;
    unit.pendingProjectileSpawned = false;
    unit.pendingProjectileTargetId = -1;
    unit.pendingProjectileSpawnTimeSec = 0.0f;
    unit.pendingProjectileTravelSec = 0.0f;

    unit.pendingImpactActive = false;
    unit.pendingImpactApplied = false;
    unit.pendingImpactTargetId = -1;
    unit.pendingImpactTimeSec = 0.0f;
    unit.pendingImpactIsGrass = false;
    unit.pendingImpactIsLeechSeed = false;

    unit.pendingDamageActive = false;
    unit.pendingDamageApplied = false;
    unit.pendingDamageTargetId = -1;
    unit.pendingDamageAmount = 0;
    unit.pendingDamageHitTimeSec = 0.0f;
    unit.pendingDamageMoveName.clear();
    unit.pendingDamageIsGrass = false;
    unit.pendingDamageIsTackle = false;
    unit.activeAttackMoveName.clear();
}

}  // namespace

void GameWorld::tickPokemonAnimation(PokemonInstance& unit, float dt) {
    if (unit.fainting) {
        updateFaint(unit, dt);
        return;
    }
    if (!unit.alive) return;

    unit.fastChainTimerSec = std::max(0.0f, unit.fastChainTimerSec - dt);

    // Attack one-shot has priority (only used when attackTimerSec > 0).
    if (unit.attackTimerSec > 0.0f) {
        const int attackAnim = (unit.currentAttackAnimIndex >= 0) ? unit.currentAttackAnimIndex : unit.animAttack1Index;

        if (unit.activeAnimIndex != attackAnim) {
            unit.activeAnimIndex = attackAnim;
            unit.animTimeSec = 0.0f;
        }

        // Run timer down.
        unit.attackTimerSec = std::max(0.0f, unit.attackTimerSec - dt);

        // Combat trace: enabled via env (PAC_TRACE_ALL / PAC_TRACE_COMBAT).
        const bool traceCombat = DebugTrace::combat(unit.name, unit.chainedFastMove);
        if (traceCombat) {
            static std::unordered_map<int, float> prevAtkTimer;
            static std::unordered_map<int, float> prevAnimTime;
            const float prevTimer = prevAtkTimer[unit.id];
            const float prevAnim = prevAnimTime[unit.id];
            prevAtkTimer[unit.id] = unit.attackTimerSec;
            prevAnimTime[unit.id] = unit.animTimeSec;

            game::log::infoTerminalOnly(log, std::string("[TRACE_COMBAT_TICK] ") +
                "unit=" + unit.name + " move=" + (unit.chainedFastMove.empty() ? std::string("-") : unit.chainedFastMove) + " " +
                "id=" + std::to_string(unit.id) +
                " dt=" + std::to_string(dt) +
                " atkTimer=" + std::to_string(unit.attackTimerSec) +
                " animTime=" + std::to_string(unit.animTimeSec) +
                " activeAnimIdx=" + std::to_string(unit.activeAnimIndex) +
                " curAtkAnimIdx=" + std::to_string(unit.currentAttackAnimIndex) +
                " speed=" + std::to_string(unit.attackAnimSpeed) +
                " prevAtkTimer=" + std::to_string(prevTimer) +
                " prevAnimTime=" + std::to_string(prevAnim));
        }

        // Clamp at last frame (avoid looping).
        const float speed = (unit.attackAnimSpeed > 0.0f) ? unit.attackAnimSpeed : 1.0f;
        const float duration =
            (unit.model && unit.activeAnimIndex >= 0)
                ? unit.model->getAnimationDurationSec(unit.activeAnimIndex)
                : std::max(0.0f, unit.attackDurationSec);
        if (duration > 0.0f) {
            unit.animTimeSec = std::min(unit.animTimeSec + dt * speed, duration - 0.0001f);

            if (traceCombat && (unit.animTimeSec >= duration - 0.00011f)) {
                game::log::infoTerminalOnly(log, std::string("[TRACE_COMBAT_TICK] clamped_end ") +
                    "unit=" + unit.name + " move=" + (unit.chainedFastMove.empty() ? std::string("-") : unit.chainedFastMove) + " " +
                    "id=" + std::to_string(unit.id) +
                    " dur=" + std::to_string(duration) +
                    " animTime=" + std::to_string(unit.animTimeSec));
            }
        } else {
            unit.animTimeSec += dt * speed;
        }

        // Spawn pending leech seed projectile at the configured spawn time.
        if (unit.pendingProjectileActive && !unit.pendingProjectileSpawned) {
            if (unit.animTimeSec >= unit.pendingProjectileSpawnTimeSec) {
                auto itTarget = std::find_if(pokemons.begin(), pokemons.end(),
                    [&](const PokemonInstance& other){ return other.id == unit.pendingProjectileTargetId; });

                if (itTarget != pokemons.end() && itTarget->alive && !itTarget->captureInProgress) {
                    leechSeedVfx.emit(unit, *itTarget, unit.pendingProjectileTravelSec);
                }

                unit.pendingProjectileSpawned = true;
            }
        }

        // Apply pending impact (for leech seed and non-damage impacts).
        if (unit.pendingImpactActive && !unit.pendingImpactApplied) {
            if (unit.animTimeSec >= unit.pendingImpactTimeSec) {
                auto itTarget = std::find_if(pokemons.begin(), pokemons.end(),
                    [&](const PokemonInstance& other){ return other.id == unit.pendingImpactTargetId; });

                if (itTarget != pokemons.end() && itTarget->alive && !itTarget->captureInProgress) {
                    if (unit.pendingImpactIsGrass) {
                        emitGrassImpactAt(*itTarget);
                    }
                    if (unit.pendingImpactIsLeechSeed) {
                        applyLeechSeed(unit.id, itTarget->id);
                    }
                }

                unit.pendingImpactApplied = true;
                unit.pendingImpactActive = false;
                unit.pendingImpactTargetId = -1;
                unit.pendingImpactTimeSec = 0.0f;
                unit.pendingImpactIsGrass = false;
                unit.pendingImpactIsLeechSeed = false;
            }
        }

        // Apply pending damage at the configured hit time (clip-time seconds).
        if (unit.pendingDamageActive && !unit.pendingDamageApplied) {
            if (unit.animTimeSec >= unit.pendingDamageHitTimeSec) {
                using Clock = std::chrono::steady_clock;
                const bool traceScratchPending =
                    game::scratch_trace::shouldTrace(
                        engineServices,
                        unit.pendingDamageMoveName);
                const auto traceStart = Clock::now();
                double lookupMs = 0.0;
                double impactMs = 0.0;
                double faintMs = 0.0;
                int hpBefore = -1;
                int hpAfter = -1;
                bool targetValid = false;

                const auto lookupStart =
                    traceScratchPending ? Clock::now() : Clock::time_point{};
                auto itTarget = std::find_if(pokemons.begin(), pokemons.end(),
                    [&](const PokemonInstance& other){ return other.id == unit.pendingDamageTargetId; });
                if (traceScratchPending) {
                    lookupMs =
                        std::chrono::duration<double, std::milli>(Clock::now() - lookupStart).count();
                }

                if (itTarget != pokemons.end() && itTarget->alive && !itTarget->captureInProgress) {
                    targetValid = true;
                    const int damage = std::max(0, unit.pendingDamageAmount);
                    hpBefore = itTarget->hp;
                    itTarget->hp = std::max(0, itTarget->hp - damage);
                    hpAfter = itTarget->hp;
                    if (!unit.pendingDamageMoveName.empty()) {
                        const auto impactStart =
                            traceScratchPending ? Clock::now() : Clock::time_point{};
                        emitMoveImpactByName(unit.pendingDamageMoveName, *itTarget, &unit);
                        if (traceScratchPending) {
                            impactMs = std::chrono::duration<double, std::milli>(
                                           Clock::now() - impactStart)
                                           .count();
                        }
                    } else {
                        if (damage > 0 && unit.pendingDamageIsGrass) {
                            emitGrassImpactAt(*itTarget);
                        }
                        if (damage > 0 && unit.pendingDamageIsTackle) {
                            emitTackleImpactAt(*itTarget, &unit);
                        }
                    }
                    if (itTarget->hp <= 0) {
                        const auto faintStart =
                            traceScratchPending ? Clock::now() : Clock::time_point{};
                        handleUnitFaint(*itTarget);
                        if (traceScratchPending) {
                            faintMs = std::chrono::duration<double, std::milli>(
                                          Clock::now() - faintStart)
                                          .count();
                        }
                    }
                }

                if (traceScratchPending) {
                    std::ostringstream trace;
                    trace << std::fixed << std::setprecision(2)
                          << "attacker=" << unit.id
                          << " target=" << unit.pendingDamageTargetId
                          << " valid_target=" << (targetValid ? 1 : 0)
                          << " damage=" << std::max(0, unit.pendingDamageAmount)
                          << " hp_before=" << hpBefore
                          << " hp_after=" << hpAfter
                          << " anim=" << unit.animTimeSec
                          << " hit_time=" << unit.pendingDamageHitTimeSec
                          << " lookup=" << lookupMs << "ms"
                          << " impact=" << impactMs << "ms"
                          << " faint=" << faintMs << "ms"
                          << " total=" <<
                                 std::chrono::duration<double, std::milli>(Clock::now() - traceStart).count()
                          << "ms";
                    game::scratch_trace::emit(log, "world_pending_damage", trace.str());
                }

                unit.pendingDamageApplied = true;
                unit.pendingDamageMoveName.clear();
                unit.pendingDamageIsGrass = false;
                unit.pendingDamageIsTackle = false;
                unit.pendingProjectileActive = false;
                unit.pendingProjectileSpawned = false;
                unit.pendingProjectileTargetId = -1;
                unit.pendingProjectileSpawnTimeSec = 0.0f;
                unit.pendingProjectileTravelSec = 0.0f;
            }
        }

        // When done, return to locomotion.
        if (unit.attackTimerSec <= 0.0f) {
            if (traceCombat) {
                game::log::infoTerminalOnly(log, std::string("[TRACE_COMBAT_TICK] attack_end ") +
                    "unit=" + unit.name + " move=" + (unit.chainedFastMove.empty() ? std::string("-") : unit.chainedFastMove) + " " +
                    "id=" + std::to_string(unit.id) +
                    " finalAnimTime=" + std::to_string(unit.animTimeSec) +
                    " activeAnimIdx=" + std::to_string(unit.activeAnimIndex));
            }
            unit.animTimeSec = 0.0f;
            unit.attackAnimSpeed = 1.0f;
            unit.currentAttackAnimIndex = unit.animAttack1Index;
            unit.activeAnimIndex = (unit.isMoving ? unit.animMoveIndex
                                                  : (unit.usesAirLocomotion ? unit.animGroundIdleIndex : unit.animIdleIndex));
            clearPendingAttackState(unit);
        }
        return;
    }

    // Locomotion (includes optional airborne takeoff/landing visuals).
    FlightLocomotion::tick(unit, dt, sharedLoopAnimTimeSec);
}

void GameWorld::update(float dt)
{
    // Evolution is level-up driven only (see addXp / mergeOneTripleForPlayer).
    reconcileBoardScaleFromRoster();

    if (boardResizePauseSec > 0.0f) {
        boardResizePauseSec = std::max(0.0f, boardResizePauseSec - dt);
        return;
    }

    // Shared clock so all units loop idle/walk in sync.
    sharedLoopAnimTimeSec += dt;

    for (auto& unit : pokemons) {
        tickPokemonAnimation(unit, dt);
    }
    for (auto& unit : benchPokemons) {
        tickPokemonAnimation(unit, dt);
    }

    // Gameplay effects (XP / leech seed) should always update.
    updateLeechSeedStatus(dt);
    updateCaptureAttempts(dt);

    updateRenderVfx(dt);
}

