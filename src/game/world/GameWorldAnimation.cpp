#include "game/world/GameWorld.h"

#include <algorithm>
#include <string>
#include <unordered_map>

#include "game/animation/FlightLocomotion.h"

#include "engine/render/Model.h"

#include "game/logging/DebugTrace.h"
#include "game/logging/LoggerUtil.h"

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
}

}  // namespace

void GameWorld::tickPokemonAnimation(PokemonInstance& unit, float dt) {
    if (!unit.model) return;
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
        const float duration = unit.model->getAnimationDurationSec(unit.activeAnimIndex);
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
                    if (renderEnabled) {
                        leechSeedVfx.emit(unit, *itTarget, unit.pendingProjectileTravelSec);
                    }
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
                auto itTarget = std::find_if(pokemons.begin(), pokemons.end(),
                    [&](const PokemonInstance& other){ return other.id == unit.pendingDamageTargetId; });

                if (itTarget != pokemons.end() && itTarget->alive && !itTarget->captureInProgress) {
                    const int damage = std::max(0, unit.pendingDamageAmount);
                    itTarget->hp = std::max(0, itTarget->hp - damage);
                    if (!unit.pendingDamageMoveName.empty()) {
                        emitMoveImpactByName(unit.pendingDamageMoveName, *itTarget, &unit);
                    } else {
                        if (damage > 0 && unit.pendingDamageIsGrass) {
                            emitGrassImpactAt(*itTarget);
                        }
                        if (damage > 0 && unit.pendingDamageIsTackle) {
                            emitTackleImpactAt(*itTarget, &unit);
                        }
                    }
                    if (itTarget->hp <= 0) {
                        handleUnitFaint(*itTarget);
                    }
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

void GameWorld::updateRenderVfx(float dt) {
    // Tail fire VFX: init once, then update every frame.
    if (!tailFireVfxInitialized) {
        // Currently only configured for Charmander (via filter + cfg section).
        tailFireVfx.setNameFilterCaseInsensitive("charmander");

        TailFireVFX::Config configData; // defaults
        TailFireVFXConfigDB::get().ensureLoaded();          // assets/config/tail_fire_vfx.cfg
        TailFireVFXConfigDB::get().applyIfAny("charmander", configData);

        tailFireVfx.setConfig(configData);
        tailFireVfxInitialized = true;
    }

    tailFireVfx.update(dt, pokemons, benchPokemons);

    if (!grassImpactVfxInitialized) {
        GrassImpactVFX::Config configData; // defaults
        grassImpactVfx.setConfig(configData);
        grassImpactVfxInitialized = true;
    }

    if (!tackleImpactVfxInitialized) {
        TackleImpactVFX::Config configData; // defaults
        tackleImpactVfx.setConfig(configData);
        tackleImpactVfxInitialized = true;
    }

    grassImpactVfx.update(dt);
    tackleImpactVfx.update(dt);

    if (!leechSeedVfxInitialized) {
        LeechSeedProjectileVFX::Config configData; // defaults
        leechSeedVfx.setConfig(configData);
        leechSeedVfxInitialized = true;
    }

    leechSeedVfx.update(dt);

    if (!healPlusVfxInitialized) {
        HealPlusVFX::Config configData; // defaults
        healPlusVfx.setConfig(configData);
        healPlusVfxInitialized = true;
    }

    if (!leechSeedDrainVfxInitialized) {
        LeechSeedDrainVFX::Config configData; // defaults
        leechSeedDrainVfx.setConfig(configData);
        leechSeedDrainVfxInitialized = true;
    }

    if (!growlWaveVfxInitialized) {
        GrowlWaveVFX::Config configData; // defaults
        // We now resolve the origin from head/jaw nodes, so keep built-in offsets minimal.
        configData.spawnForwardOffset = 0.0f;
        configData.spawnHeightOffset = 0.0f;
        growlWaveVfx.setConfig(configData);
        growlWaveVfxInitialized = true;
    }

    if (!clawSwipeVfxInitialized) {
        ClawSwipeVFX::Config configData; // defaults
        clawSwipeVfx.setConfig(configData);
        clawSwipeVfxInitialized = true;
    }

    if (!aquaSwooshVfxInitialized) {
        AquaSwooshVFX::Config configData; // defaults
        aquaSwooshVfx.setConfig(configData);
        aquaSwooshVfxInitialized = true;
    }

    healPlusVfx.update(dt);
    leechSeedDrainVfx.update(dt);
    growlWaveVfx.update(dt);
    clawSwipeVfx.update(dt);
    aquaSwooshVfx.update(dt);
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

    if (!renderEnabled) return;
    updateRenderVfx(dt);
}

