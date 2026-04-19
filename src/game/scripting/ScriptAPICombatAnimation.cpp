#include "game/scripting/ScriptAPICombatInternal.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

#include <glm/glm.hpp>

#include "engine/render/Model.h"

#include "game/GameWorld.h"
#include "game/animation/AttackAnimDebug.h"
#include "game/animation/FlightLocomotion.h"
#include "game/config/GameDataDb.h"
#include "game/logging/DebugTrace.h"

#include "LuaBindings_Internal.h"

namespace {

constexpr float kLeechSeedSpawnFrame = 41.0f;
constexpr float kLeechSeedSpeed = 5.0f;  // world units per second
constexpr float kLeechSeedMinTravelSec = 0.12f;
constexpr float kLeechSeedMaxTravelSec = 0.55f;

float computeLeechSeedTravelSec(float distance) {
    if (distance <= 0.0f) return kLeechSeedMinTravelSec;
    const float travelSec = distance / kLeechSeedSpeed;
    return std::clamp(travelSec, kLeechSeedMinTravelSec, kLeechSeedMaxTravelSec);
}

void resetPendingAttackState(PokemonInstance& attacker) {
    attacker.pendingDamageActive = false;
    attacker.pendingDamageApplied = false;
    attacker.pendingDamageTargetId = -1;
    attacker.pendingDamageAmount = 0;
    attacker.pendingDamageHitTimeSec = 0.0f;
    attacker.pendingDamageMoveName.clear();
    attacker.pendingDamageIsGrass = false;
    attacker.pendingDamageIsTackle = false;
    attacker.pendingProjectileActive = false;
    attacker.pendingProjectileSpawned = false;
    attacker.pendingProjectileTargetId = -1;
    attacker.pendingProjectileSpawnTimeSec = 0.0f;
    attacker.pendingProjectileTravelSec = 0.0f;
    attacker.pendingImpactActive = false;
    attacker.pendingImpactApplied = false;
    attacker.pendingImpactTargetId = -1;
    attacker.pendingImpactTimeSec = 0.0f;
    attacker.pendingImpactIsGrass = false;
    attacker.pendingImpactIsLeechSeed = false;
    attacker.activeAttackMoveName.clear();
}

}  // namespace

namespace scriptapi::combat {

bool tryBeginAttackAnimation(PokemonInstance& attacker,
                             GameWorld& world,
                             PokemonInstance& target,
                             int targetId,
                             int amount,
                             const std::optional<float>& cadenceSec,
                             const DamageContext& ctx,
                             const GameDataDb* data,
                             LogBus::Logger* log,
                             const TraceContext& trace,
                             int& outResultHp) {
    if (attacker.attackDurationSec <= 0.0f || attacker.animAttack1Index < 0) return false;

    bool airborne = false;
    if (attacker.usesAirLocomotion) {
        airborne = FlightLocomotion::isAirborne(attacker);
    }

    float desiredWindowSec = cadenceSec.value_or(0.0f);
    if (desiredWindowSec <= 0.0f) desiredWindowSec = attacker.attackDurationSec;

    const auto* animCfg = data ? &data->attackAnims : nullptr;
    const float minReqSec =
        animCfg ? animCfg->getMinRequestSec(ctx.speciesLower, ctx.kindLower, ctx.moveLower, log) : 0.0f;
    if (minReqSec > 0.0f) desiredWindowSec = std::max(desiredWindowSec, minReqSec);

    if (trace.enabled) {
        const float baseCadence = cadenceSec.value_or(0.0f);
        const float attackClipDuration =
            (attacker.model && attacker.animAttack1Index >= 0)
                ? attacker.model->getAnimationDurationSec(attacker.animAttack1Index)
                : 0.0f;
        traceLog(trace,
                 std::string("cadence baseCadenceArg=") + std::to_string(baseCadence) +
                     " attackDurationSec=" + std::to_string(attacker.attackDurationSec) +
                     " clipDur_attack1=" + std::to_string(attackClipDuration) +
                     " minReqSec=" + std::to_string(minReqSec) +
                     " desiredWindowSec=" + std::to_string(desiredWindowSec));
    }

    constexpr float kMidCycleEps = 0.0001f;
    if (attacker.attackTimerSec > kMidCycleEps) {
        traceLog(trace, "lock: mid-cycle -> ignore request (no new cycle, no damage)");
        outResultHp = target.hp;
        return true;
    }

    int desiredAnimIdx = attacker.animAttack1Index;
    std::string phase = "default";
    std::string clipUsed;

    if (!ctx.speciesLower.empty()) {
        if (ctx.kindLower == "charged") {
            phase = "one_shot";
            clipUsed = animCfg
                           ? animCfg->getClipName(ctx.speciesLower, "charged", ctx.moveLower, "one_shot", log)
                           : std::string();
            if (clipUsed.empty()) {
                phase = "start";
                clipUsed = animCfg
                               ? animCfg->getClipName(ctx.speciesLower, "charged", ctx.moveLower, "start", log)
                               : std::string();
            }
            if (clipUsed.empty()) {
                phase = "default";
                clipUsed = animCfg
                               ? animCfg->getClipName(ctx.speciesLower, "charged", ctx.moveLower, "default", log)
                               : std::string();
            }
            const int idx = animIndexCached(attacker, clipUsed);
            if (idx >= 0) desiredAnimIdx = idx;
        } else if (ctx.kindLower == "fast" && !ctx.moveLower.empty()) {
            const std::string clipLoop =
                animCfg ? animCfg->getClipName(ctx.speciesLower, "fast", ctx.moveLower, "loop", log) : std::string();
            const std::string clipDefault =
                animCfg ? animCfg->getClipName(ctx.speciesLower, "fast", ctx.moveLower, "default", log)
                        : std::string();

            phase = "loop";
            clipUsed = clipLoop;
            if (clipUsed.empty()) {
                phase = "default";
                clipUsed = clipDefault;
            }

            if (!clipUsed.empty()) {
                const int idx = animIndexCached(attacker, clipUsed);
                if (idx >= 0) desiredAnimIdx = idx;
            }

            attacker.chainedFastMove.clear();
            attacker.fastChainTimerSec = 0.0f;
        }
    }

    if (trace.enabled) {
        const float clipDur =
            (attacker.model && desiredAnimIdx >= 0) ? attacker.model->getAnimationDurationSec(desiredAnimIdx) : 0.0f;
        traceLog(trace,
                 std::string("resolved desiredAnimIdx=") + std::to_string(desiredAnimIdx) +
                     " clipDur=" + std::to_string(clipDur) +
                     " fastChainTimerSec=" + std::to_string(attacker.fastChainTimerSec) +
                     " chainedFastMove=" +
                     (attacker.chainedFastMove.empty() ? std::string("-") : attacker.chainedFastMove));
    }

#ifdef PAC_DEBUG_ANIM
    std::cout << "[AnimDebug] " << attacker.name << " (ID " << attacker.id << ") "
              << "attack requested airborne=" << (airborne ? "true" : "false")
              << " dmg=" << amount
              << " cadence=" << desiredWindowSec
              << " animIdx=" << desiredAnimIdx
              << " clipDur="
              << (attacker.model ? attacker.model->getAnimationDurationSec(desiredAnimIdx) : 0.0f) << "\n";
#endif

    if (airborne) {
        attacker.activeAttackMoveName = ctx.moveLower;
        FlightLocomotion::queueAttackAfterLanding(attacker, desiredWindowSec, desiredAnimIdx);
        outResultHp = target.hp;
        return true;
    }

    const float clipDur =
        attacker.model ? attacker.model->getAnimationDurationSec(desiredAnimIdx) : attacker.attackDurationSec;
    const float windowSec = std::max(0.05f, desiredWindowSec);

    attacker.attackTimerSec = windowSec;
    attacker.animTimeSec = 0.0f;
    attacker.currentAttackAnimIndex = desiredAnimIdx;
    attacker.activeAnimIndex = desiredAnimIdx;
    attacker.attackAnimSpeed = (windowSec > 0.0f && clipDur > 0.0f) ? (clipDur / windowSec) : 1.0f;
    resetPendingAttackState(attacker);
    attacker.activeAttackMoveName = ctx.moveLower;

    if (trace.enabled) {
        traceLog(trace,
                 std::string("attack_state startedThisCall=true") +
                     " windowSec=" + std::to_string(windowSec) +
                     " clipDur=" + std::to_string(clipDur) +
                     " activeAnimIdx=" + std::to_string(attacker.activeAnimIndex) +
                     " currentAttackAnimIndex=" + std::to_string(attacker.currentAttackAnimIndex) +
                     " atkAnimSpeed=" + std::to_string(attacker.attackAnimSpeed) +
                     " atkTimer=" + std::to_string(attacker.attackTimerSec));
    }

    if (attacker.debugAnimLogs || trace.animationDecision ||
        DebugTrace::anim(attacker.name, ctx.moveLower)) {
        const int hpBefore = target.hp;
        const bool willKill = (std::max(0, amount) > 0 && (hpBefore - std::max(0, amount) <= 0));
        const float clipDurDbg =
            (attacker.model && desiredAnimIdx >= 0) ? attacker.model->getAnimationDurationSec(desiredAnimIdx) : 0.0f;
        AttackAnimDebug::logSelection(attacker,
                                      ctx.kindLower,
                                      ctx.moveLower,
                                      phase,
                                      clipUsed,
                                      desiredAnimIdx,
                                      clipDurDbg,
                                      desiredWindowSec,
                                      amount,
                                      hpBefore,
                                      (std::max(0, hpBefore - std::max(0, amount))),
                                      true,
                                      willKill,
                                      attacker.fastChainTimerSec);
    }

    if (!attackerIsInAttackAnimation(attacker)) {
        outResultHp = target.hp;
        return true;
    }

    if (ctx.isLeechSeed) {
        const float fps = (attacker.animFps > 0.0f) ? attacker.animFps : 24.0f;
        float spawnTimeClip = kLeechSeedSpawnFrame / fps;

        const glm::vec3 attackerPos = attacker.position + glm::vec3(0.0f, attacker.visualYOffset, 0.0f);
        const glm::vec3 targetPos = target.position + glm::vec3(0.0f, target.visualYOffset, 0.0f);
        const float distance = glm::distance(attackerPos, targetPos);

        float travelReal = computeLeechSeedTravelSec(distance);
        float travelClip = travelReal * attacker.attackAnimSpeed;

        if (clipDur > 0.0f) {
            const float maxHit = std::max(0.0f, clipDur - 0.0001f);
            if (spawnTimeClip > maxHit) spawnTimeClip = maxHit;
            if (spawnTimeClip + travelClip > maxHit) {
                travelClip = std::max(0.0f, maxHit - spawnTimeClip);
                travelReal = (attacker.attackAnimSpeed > 0.0f) ? (travelClip / attacker.attackAnimSpeed) : travelReal;
            }
        }

        if (!attacker.pendingImpactActive) {
            attacker.pendingImpactActive = true;
            attacker.pendingImpactApplied = false;
            attacker.pendingImpactTargetId = targetId;
            attacker.pendingImpactTimeSec = std::max(0.0f, spawnTimeClip + travelClip);
            attacker.pendingImpactIsGrass = ctx.isGrassImpact;
            attacker.pendingImpactIsLeechSeed = true;

            attacker.pendingProjectileActive = true;
            attacker.pendingProjectileSpawned = false;
            attacker.pendingProjectileTargetId = targetId;
            attacker.pendingProjectileSpawnTimeSec = std::max(0.0f, spawnTimeClip);
            attacker.pendingProjectileTravelSec = std::max(0.01f, travelReal);
        }

        outResultHp = target.hp;
        return true;
    }

    const int hitFrame =
        animCfg ? animCfg->getHitFrame(ctx.speciesLower, ctx.kindLower, ctx.moveLower) : -1;
    if (hitFrame > 0) {
        if (!attacker.pendingDamageActive) {
            const float fps = (attacker.animFps > 0.0f) ? attacker.animFps : 24.0f;
            float hitTimeSec = static_cast<float>(hitFrame) / fps;

            const float clipDurClamp =
                (attacker.model && attacker.currentAttackAnimIndex >= 0)
                    ? attacker.model->getAnimationDurationSec(attacker.currentAttackAnimIndex)
                    : 0.0f;
            if (clipDurClamp > 0.0f) {
                const float maxT = std::max(0.0f, clipDurClamp - 0.0001f);
                hitTimeSec = std::min(hitTimeSec, maxT);
            }

            attacker.pendingDamageActive = true;
            attacker.pendingDamageApplied = false;
            attacker.pendingDamageTargetId = targetId;
            attacker.pendingDamageAmount = std::max(0, amount);
            attacker.pendingDamageHitTimeSec = std::max(0.0f, hitTimeSec);
            attacker.pendingDamageMoveName = ctx.moveLower;
            attacker.pendingDamageIsGrass = ctx.isGrassImpact;
            attacker.pendingDamageIsTackle = ctx.isTackle;
        }

        outResultHp = (amount <= 0) ? target.hp : std::max(0, target.hp - std::max(0, amount));
        return true;
    }

    if (amount <= 0) {
        world.emitMoveImpactByName(ctx.moveLower, target, &attacker);
        outResultHp = target.hp;
        return true;
    }

    return false;
}

}  // namespace scriptapi::combat
