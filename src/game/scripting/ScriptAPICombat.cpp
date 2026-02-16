#include "game/scripting/ScriptAPI.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <glm/glm.hpp>

#include "engine/render/Model.h"

#include "game/GameWorld.h"
#include "game/animation/AttackAnimDebug.h"
#include "game/animation/FlightLocomotion.h"
#include "game/config/GameDataDb.h"
#include "game/logging/DebugTrace.h"
#include "game/logging/LoggerUtil.h"

#include "LuaBindings_Internal.h"

namespace {
constexpr float kLeechSeedSpawnFrame = 41.0f;
constexpr float kLeechSeedSpeed = 5.0f;          // world units per second
constexpr float kLeechSeedMinTravelSec = 0.12f;
constexpr float kLeechSeedMaxTravelSec = 0.55f;

float computeLeechSeedTravelSec(float distance) {
    if (distance <= 0.0f) return kLeechSeedMinTravelSec;
    float t = distance / kLeechSeedSpeed;
    return std::clamp(t, kLeechSeedMinTravelSec, kLeechSeedMaxTravelSec);
}

bool isCombatActive(const PokemonInstance& u) {
    return u.alive && !u.captureInProgress;
}
}  // namespace

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
    if (!isCombatActive(*A)) return T->hp;
    if (T->captureInProgress) return T->hp;

    const std::string speciesLower = toLowerCopy(A->name);
    const std::string moveLower    = moveName ? toLowerCopy(*moveName) : "";
    std::string kindLower          = kind ? toLowerCopy(*kind) : "";

    const MoveData* md = nullptr;
    if (!moveLower.empty() && data) {
        md = data->moves.getMove(moveLower);
    }

    if (kindLower.empty() && md) {
        kindLower = toLowerCopy(md->kind);
    }
    if (kindLower.empty()) kindLower = "fast";

    const std::string moveTypeLower = md ? toLowerCopy(md->type) : std::string();
    const bool isGrassMove = (moveTypeLower == "grass");
    const bool isLeechSeed = (moveLower == "leech_seed");
    const bool isTackle = (moveLower == "tackle");
    const bool isGrassImpact = (isGrassMove || isLeechSeed);

    const bool traceCombat = DebugTrace::combat(speciesLower, moveLower);
    auto trlog = [&](const std::string& msg) {
        if (!traceCombat) return;
        game::log::infoTerminalOnly(&services_.log, std::string("[TRACE_COMBAT_CPP] ") +
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
            ? animCfg->getMinRequestSec(speciesLower, kindLower, moveLower, &services_.log)
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
                    ? animCfg->getClipName(speciesLower, "charged", moveLower, "one_shot", &services_.log)
                    : std::string();
                if (clipUsed.empty()) {
                    phase = "start";
                    clipUsed = animCfg
                        ? animCfg->getClipName(speciesLower, "charged", moveLower, "start", &services_.log)
                        : std::string();
                }
                if (clipUsed.empty()) {
                    phase = "default";
                    clipUsed = animCfg
                        ? animCfg->getClipName(speciesLower, "charged", moveLower, "default", &services_.log)
                        : std::string();
                }
                const int idx = animIndexCached(*A, clipUsed);
                if (idx >= 0) desiredAnimIdx = idx;
            } else if (kindLower == "fast" && !moveLower.empty()) {
                const std::string clipLoop = animCfg
                    ? animCfg->getClipName(speciesLower, "fast", moveLower, "loop", &services_.log)
                    : std::string();
                const std::string clipDef  = animCfg
                    ? animCfg->getClipName(speciesLower, "fast", moveLower, "default", &services_.log)
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
        A->pendingDamageMoveName.clear();
        A->pendingDamageIsGrass = false;
        A->pendingDamageIsTackle = false;
        A->pendingProjectileActive = false;
        A->pendingProjectileSpawned = false;
        A->pendingProjectileTargetId = -1;
        A->pendingProjectileSpawnTimeSec = 0.0f;
        A->pendingProjectileTravelSec = 0.0f;
        A->pendingImpactActive = false;
        A->pendingImpactApplied = false;
        A->pendingImpactTargetId = -1;
        A->pendingImpactTimeSec = 0.0f;
        A->pendingImpactIsGrass = false;
        A->pendingImpactIsLeechSeed = false;

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

        if (!attackerIsInAttackAnimation(*A)) return T->hp;

        if (isLeechSeed) {
            const float fps = (A->animFps > 0.0f) ? A->animFps : 24.0f;
            float spawnTimeClip = kLeechSeedSpawnFrame / fps;

            const glm::vec3 aPos = A->position + glm::vec3(0.0f, A->visualYOffset, 0.0f);
            const glm::vec3 tPos = T->position + glm::vec3(0.0f, T->visualYOffset, 0.0f);
            const float dist = glm::distance(aPos, tPos);

            float travelReal = computeLeechSeedTravelSec(dist);
            float travelClip = travelReal * A->attackAnimSpeed;

            if (clipDur > 0.0f) {
                const float maxHit = std::max(0.0f, clipDur - 0.0001f);
                if (spawnTimeClip > maxHit) spawnTimeClip = maxHit;
                if (spawnTimeClip + travelClip > maxHit) {
                    travelClip = std::max(0.0f, maxHit - spawnTimeClip);
                    travelReal = (A->attackAnimSpeed > 0.0f) ? (travelClip / A->attackAnimSpeed) : travelReal;
                }
            }

            if (!A->pendingImpactActive) {
                A->pendingImpactActive = true;
                A->pendingImpactApplied = false;
                A->pendingImpactTargetId = targetId;
                A->pendingImpactTimeSec = std::max(0.0f, spawnTimeClip + travelClip);
                A->pendingImpactIsGrass = isGrassImpact;
                A->pendingImpactIsLeechSeed = true;

                A->pendingProjectileActive = true;
                A->pendingProjectileSpawned = false;
                A->pendingProjectileTargetId = targetId;
                A->pendingProjectileSpawnTimeSec = std::max(0.0f, spawnTimeClip);
                A->pendingProjectileTravelSec = std::max(0.01f, travelReal);
            }

            // Leech Seed does not deal damage on impact.
            return T->hp;
        }

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
                A->pendingDamageMoveName   = moveLower;
                A->pendingDamageIsGrass    = isGrassImpact;
                A->pendingDamageIsTackle   = isTackle;
            }

            if (amount <= 0) return T->hp;
            return std::max(0, T->hp - std::max(0, amount));
        }

        if (amount <= 0) {
            world_->emitMoveImpactByName(moveLower, *T, &(*A));
            return T->hp;
        }
    }

    int dmg = std::max(0, amount);
    if (traceCombat) {
        trlog(std::string("damage_apply dmg=") + std::to_string(dmg) +
              " hp_before=" + std::to_string(T->hp));
    }
    T->hp = std::max(0, T->hp - dmg);
    if (!moveLower.empty()) {
        world_->emitMoveImpactByName(moveLower, *T, &(*A));
    } else {
        if (dmg > 0 && isGrassImpact) {
            world_->emitGrassImpactAt(*T);
        }
        if (dmg > 0 && isTackle) {
            world_->emitTackleImpactAt(*T, &(*A));
        }
    }
    if (traceCombat) {
        trlog(std::string("damage_result hp_after=") + std::to_string(T->hp) +
              " targetAlive=" + std::string(T->hp > 0 ? "true" : "false"));
    }

    if (T->hp <= 0) {
        world_->handleUnitFaint(*T);
    }

    return T->hp;
}
