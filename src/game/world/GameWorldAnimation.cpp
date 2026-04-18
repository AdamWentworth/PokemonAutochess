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

using WorldTraceClock = std::chrono::steady_clock;

constexpr double kWorldHitchUpdateThresholdMs = 8.0;
constexpr double kWorldPendingDamageThresholdMs = 2.0;

bool shouldTraceWorldHitch(const EngineServices* services) {
    return services && services->terminalLogMode == EngineTerminalLogMode::Performance;
}

void emitWorldHitch(LogBus::Logger* logger,
                    std::string_view stage,
                    const std::string& details) {
    game::log::infoTerminalOnly(
        logger,
        std::string("[WorldHitch] stage=") + std::string(stage) + " " + details);
}

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
                using Clock = WorldTraceClock;
                const bool traceScratchPending =
                    game::scratch_trace::shouldTrace(
                        engineServices,
                        unit.pendingDamageMoveName);
                const bool traceWorldPending = shouldTraceWorldHitch(engineServices);
                const bool tracePendingDamage = traceScratchPending || traceWorldPending;
                const auto traceStart = Clock::now();
                double lookupMs = 0.0;
                double impactMs = 0.0;
                double faintMs = 0.0;
                double totalMs = 0.0;
                int hpBefore = -1;
                int hpAfter = -1;
                bool targetValid = false;

                const auto lookupStart =
                    tracePendingDamage ? Clock::now() : Clock::time_point{};
                auto itTarget = std::find_if(pokemons.begin(), pokemons.end(),
                    [&](const PokemonInstance& other){ return other.id == unit.pendingDamageTargetId; });
                if (tracePendingDamage) {
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
                            tracePendingDamage ? Clock::now() : Clock::time_point{};
                        emitMoveImpactByName(unit.pendingDamageMoveName, *itTarget, &unit);
                        if (tracePendingDamage) {
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
                            tracePendingDamage ? Clock::now() : Clock::time_point{};
                        handleUnitFaint(*itTarget);
                        if (tracePendingDamage) {
                            faintMs = std::chrono::duration<double, std::milli>(
                                          Clock::now() - faintStart)
                                          .count();
                        }
                    }
                }

                if (tracePendingDamage) {
                    totalMs =
                        std::chrono::duration<double, std::milli>(Clock::now() - traceStart).count();
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
                          << " total=" << totalMs << "ms";
                    game::scratch_trace::emit(log, "world_pending_damage", trace.str());
                }
                if (traceWorldPending && totalMs >= kWorldPendingDamageThresholdMs) {
                    std::ostringstream trace;
                    trace << std::fixed << std::setprecision(2)
                          << "unit=" << unit.id
                          << " name=" << unit.name
                          << " move=" <<
                                 (unit.pendingDamageMoveName.empty() ? std::string("-")
                                                                     : unit.pendingDamageMoveName)
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
                          << " total=" << totalMs << "ms";
                    emitWorldHitch(log, "pending_damage", trace.str());
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
    const bool traceWorld = shouldTraceWorldHitch(engineServices);
    const auto updateStart = traceWorld ? WorldTraceClock::now() : WorldTraceClock::time_point{};
    double reconcileMs = 0.0;
    double animBoardMs = 0.0;
    double animBenchMs = 0.0;
    double leechMs = 0.0;
    double captureMs = 0.0;
    double renderVfxMs = 0.0;
    double totalMs = 0.0;
    double slowestUnitMs = 0.0;
    int slowestUnitId = -1;
    std::string slowestUnitName;
    std::string slowestMoveName;
    bool slowestWasBench = false;
    bool slowestStartedAttack = false;
    bool slowestStartedPendingDamage = false;
    bool slowestStartedFainting = false;

    const auto updateSlowestUnit = [&](const PokemonInstance& unit,
                                       double unitMs,
                                       bool wasBench,
                                       bool startedAttack,
                                       bool startedPendingDamage,
                                       bool startedFainting) {
        if (unitMs < slowestUnitMs) return;
        slowestUnitMs = unitMs;
        slowestUnitId = unit.id;
        slowestUnitName = unit.name;
        if (!unit.pendingDamageMoveName.empty()) {
            slowestMoveName = unit.pendingDamageMoveName;
        } else if (!unit.activeAttackMoveName.empty()) {
            slowestMoveName = unit.activeAttackMoveName;
        } else if (!unit.chainedFastMove.empty()) {
            slowestMoveName = unit.chainedFastMove;
        } else {
            slowestMoveName.clear();
        }
        slowestWasBench = wasBench;
        slowestStartedAttack = startedAttack;
        slowestStartedPendingDamage = startedPendingDamage;
        slowestStartedFainting = startedFainting;
    };

    const auto countUnits = [](const std::vector<PokemonInstance>& units,
                               auto&& predicate) -> int {
        return static_cast<int>(std::count_if(units.begin(), units.end(), predicate));
    };
    const int boardUnits = static_cast<int>(pokemons.size());
    const int benchUnits = static_cast<int>(benchPokemons.size());
    const int aliveUnits =
        countUnits(pokemons, [](const PokemonInstance& unit) { return unit.alive; }) +
        countUnits(benchPokemons, [](const PokemonInstance& unit) { return unit.alive; });
    const int faintingUnits =
        countUnits(pokemons, [](const PokemonInstance& unit) { return unit.fainting; }) +
        countUnits(benchPokemons, [](const PokemonInstance& unit) { return unit.fainting; });
    const int attackingUnits =
        countUnits(pokemons, [](const PokemonInstance& unit) { return unit.attackTimerSec > 0.0f; }) +
        countUnits(benchPokemons,
                   [](const PokemonInstance& unit) { return unit.attackTimerSec > 0.0f; });
    const int pendingDamageUnits =
        countUnits(pokemons,
                   [](const PokemonInstance& unit) {
                       return unit.pendingDamageActive && !unit.pendingDamageApplied;
                   }) +
        countUnits(benchPokemons,
                   [](const PokemonInstance& unit) {
                       return unit.pendingDamageActive && !unit.pendingDamageApplied;
                   });

    // Evolution is level-up driven only (see addXp / mergeOneTripleForPlayer).
    const auto reconcileStart = traceWorld ? WorldTraceClock::now() : WorldTraceClock::time_point{};
    reconcileBoardScaleFromRoster();
    if (traceWorld) {
        reconcileMs =
            std::chrono::duration<double, std::milli>(WorldTraceClock::now() - reconcileStart)
                .count();
    }

    if (boardResizePauseSec > 0.0f) {
        boardResizePauseSec = std::max(0.0f, boardResizePauseSec - dt);
        if (traceWorld) {
            totalMs =
                std::chrono::duration<double, std::milli>(WorldTraceClock::now() - updateStart)
                    .count();
            if (totalMs >= kWorldHitchUpdateThresholdMs) {
                std::ostringstream trace;
                trace << std::fixed << std::setprecision(2)
                      << "board_units=" << boardUnits
                      << " bench_units=" << benchUnits
                      << " alive=" << aliveUnits
                      << " fainting=" << faintingUnits
                      << " attacking=" << attackingUnits
                      << " pending_damage=" << pendingDamageUnits
                      << " board_pause=1"
                      << " reconcile=" << reconcileMs << "ms"
                      << " total=" << totalMs << "ms";
                emitWorldHitch(log, "update", trace.str());
            }
        }
        return;
    }

    // Shared clock so all units loop idle/walk in sync.
    sharedLoopAnimTimeSec += dt;

    const auto animBoardStart = traceWorld ? WorldTraceClock::now() : WorldTraceClock::time_point{};
    for (auto& unit : pokemons) {
        const bool startedAttack = unit.attackTimerSec > 0.0f;
        const bool startedPendingDamage =
            unit.pendingDamageActive && !unit.pendingDamageApplied;
        const bool startedFainting = unit.fainting;
        const auto unitStart = traceWorld ? WorldTraceClock::now() : WorldTraceClock::time_point{};
        tickPokemonAnimation(unit, dt);
        if (traceWorld) {
            const double unitMs =
                std::chrono::duration<double, std::milli>(WorldTraceClock::now() - unitStart)
                    .count();
            updateSlowestUnit(
                unit, unitMs, false, startedAttack, startedPendingDamage, startedFainting);
        }
    }
    if (traceWorld) {
        animBoardMs =
            std::chrono::duration<double, std::milli>(WorldTraceClock::now() - animBoardStart)
                .count();
    }

    const auto animBenchStart = traceWorld ? WorldTraceClock::now() : WorldTraceClock::time_point{};
    for (auto& unit : benchPokemons) {
        const bool startedAttack = unit.attackTimerSec > 0.0f;
        const bool startedPendingDamage =
            unit.pendingDamageActive && !unit.pendingDamageApplied;
        const bool startedFainting = unit.fainting;
        const auto unitStart = traceWorld ? WorldTraceClock::now() : WorldTraceClock::time_point{};
        tickPokemonAnimation(unit, dt);
        if (traceWorld) {
            const double unitMs =
                std::chrono::duration<double, std::milli>(WorldTraceClock::now() - unitStart)
                    .count();
            updateSlowestUnit(
                unit, unitMs, true, startedAttack, startedPendingDamage, startedFainting);
        }
    }
    if (traceWorld) {
        animBenchMs =
            std::chrono::duration<double, std::milli>(WorldTraceClock::now() - animBenchStart)
                .count();
    }

    // Gameplay effects (XP / leech seed) should always update.
    const auto leechStart = traceWorld ? WorldTraceClock::now() : WorldTraceClock::time_point{};
    updateLeechSeedStatus(dt);
    if (traceWorld) {
        leechMs =
            std::chrono::duration<double, std::milli>(WorldTraceClock::now() - leechStart)
                .count();
    }
    const auto captureStart = traceWorld ? WorldTraceClock::now() : WorldTraceClock::time_point{};
    updateCaptureAttempts(dt);
    if (traceWorld) {
        captureMs =
            std::chrono::duration<double, std::milli>(WorldTraceClock::now() - captureStart)
                .count();
    }

    const auto renderVfxStart =
        traceWorld ? WorldTraceClock::now() : WorldTraceClock::time_point{};
    updateRenderVfx(dt);
    if (traceWorld) {
        renderVfxMs =
            std::chrono::duration<double, std::milli>(WorldTraceClock::now() - renderVfxStart)
                .count();
        totalMs =
            std::chrono::duration<double, std::milli>(WorldTraceClock::now() - updateStart)
                .count();
        if (totalMs >= kWorldHitchUpdateThresholdMs) {
            std::ostringstream trace;
            trace << std::fixed << std::setprecision(2)
                  << "board_units=" << boardUnits
                  << " bench_units=" << benchUnits
                  << " alive=" << aliveUnits
                  << " fainting=" << faintingUnits
                  << " attacking=" << attackingUnits
                  << " pending_damage=" << pendingDamageUnits
                  << " reconcile=" << reconcileMs << "ms"
                  << " anim_board=" << animBoardMs << "ms"
                  << " anim_bench=" << animBenchMs << "ms"
                  << " leech=" << leechMs << "ms"
                  << " capture=" << captureMs << "ms"
                  << " render_vfx=" << renderVfxMs << "ms"
                  << " total=" << totalMs << "ms";
            emitWorldHitch(log, "update", trace.str());

            if (slowestUnitId >= 0) {
                std::ostringstream unitTrace;
                unitTrace << std::fixed << std::setprecision(2)
                          << "unit=" << slowestUnitId
                          << " name=" << slowestUnitName
                          << " move=" <<
                                 (slowestMoveName.empty() ? std::string("-") : slowestMoveName)
                          << " bench=" << (slowestWasBench ? 1 : 0)
                          << " started_faint=" << (slowestStartedFainting ? 1 : 0)
                          << " started_attack=" << (slowestStartedAttack ? 1 : 0)
                          << " started_pending_damage="
                          << (slowestStartedPendingDamage ? 1 : 0)
                          << " total=" << slowestUnitMs << "ms";
                emitWorldHitch(log, "slowest_unit", unitTrace.str());
            }
        }
    }
}

