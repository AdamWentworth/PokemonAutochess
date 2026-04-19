// src/game/FlightLocomotion.cpp
#include "FlightLocomotion.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <iostream>
#include <string>
#include <string_view>

#include "engine/render/Model.h"
#include "game/logging/DebugTrace.h"

namespace FlightLocomotion {

static float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
static float smoothstep01(float t) {
    t = clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

static std::string_view traceMoveName(const PokemonInstance& p) {
    if (!p.activeAttackMoveName.empty()) return p.activeAttackMoveName;
    if (!p.pendingDamageMoveName.empty()) return p.pendingDamageMoveName;
    if (!p.chainedFastMove.empty()) return p.chainedFastMove;
    if (!p.fastMove.empty()) return p.fastMove;
    return {};
}

static bool isDebug(const PokemonInstance& p) {
    return p.debugAnimLogs || DebugTrace::anim(p.name, traceMoveName(p));
}

static float clipDurationSec(const PokemonInstance& p, int animIndex)
{
    if (animIndex < 0) return 0.0f;
    if (p.model) {
        return p.model->getAnimationDurationSec(animIndex);
    }
    if (static_cast<std::size_t>(animIndex) < p.backendAnimDurationsSec.size()) {
        return std::max(0.0f, p.backendAnimDurationsSec[static_cast<std::size_t>(animIndex)]);
    }
    return 0.0f;
}

bool isAirborne(const PokemonInstance& p)
{
    return p.usesAirLocomotion && p.airState != AirLocomotionState::Grounded;
}

static const char* stateName(AirLocomotionState s) {
    switch (s) {
        case AirLocomotionState::Grounded:      return "Grounded";
        case AirLocomotionState::TakingOff:     return "TakingOff";
        case AirLocomotionState::Airborne:      return "Airborne";
        case AirLocomotionState::LandingStart:  return "LandingStart";
        case AirLocomotionState::LandingLoop:   return "LandingLoop";
        case AirLocomotionState::LandingFinish: return "LandingFinish";
    }
    return "Unknown";
}

static void logTransition(const PokemonInstance& p,
                                 AirLocomotionState from,
                                 AirLocomotionState to,
                                 float elapsedAnimSec,
                                 float speed,
                                 float targetAnimSec,
                                 const char* reason,
                                 int anim,
                                 const char* role,
                                 float clipDur)
{
    if (!isDebug(p)) return;

    const float elapsedReal = (speed > 0.0f) ? (elapsedAnimSec / speed) : elapsedAnimSec;
    const float targetReal  = (speed > 0.0f) ? (targetAnimSec / speed) : targetAnimSec;

    std::cout << "[AnimDebug] " << p.name << " (ID " << p.id << ") "
              << stateName(from) << " -> " << stateName(to)
              << " after " << elapsedReal << "s"
              << " (target " << targetReal << "s)"
              << " reason=" << (reason ? reason : "-")
              << " anim=" << anim
              << " role=" << (role ? role : "-")
              << " clipDur=" << clipDur
              << "\n";
}

static void syncLoopTime(PokemonInstance& p, float sharedLoopTimeSec)
{
    if (p.activeAnimIndex < 0) {
        p.animTimeSec = sharedLoopTimeSec;
        return;
    }

    const float dur = clipDurationSec(p, p.activeAnimIndex);
    if (dur > 0.0f) {
        p.animTimeSec = std::fmod(sharedLoopTimeSec, dur);
    } else {
        p.animTimeSec = sharedLoopTimeSec;
    }
}

// Compute landing loop (B) target duration in animation-seconds.
static float computeLandingLoopTargetAnimSec(const PokemonInstance& p,
                                                   float durA, float durB, float durC)
{
    if (durB <= 0.0f) return 0.0f;

    float total = 0.0f;

    if (p.landingSec > 0.0f) {
        total = std::max(0.0f, p.landingSec);
    } else {
        total = std::max(0.0f, durA + durB + durC);
    }

    float loopTarget = total - durA - durC;
    if (loopTarget <= 0.0f) loopTarget = durB;
    return std::max(0.0f, loopTarget);
}

static void beginTakeoff(PokemonInstance& p)
{
    if (!p.usesAirLocomotion) return;
    if (p.airState == AirLocomotionState::TakingOff || p.airState == AirLocomotionState::Airborne) return;

    const AirLocomotionState from = p.airState;

    p.airState = AirLocomotionState::TakingOff;
    p.airStateTimeSec = 0.0f;

    if (p.animTakeoffIndex >= 0) {
        p.activeAnimIndex = p.animTakeoffIndex;
        p.animTimeSec = 0.0f;
    }

    if (isDebug(p)) {
        logTransition(p, from, p.airState, 0.0f, std::max(0.05f, p.takeoffAnimSpeed),
                      0.0f, "beginTakeoff", p.activeAnimIndex, "takeoff",
                      clipDurationSec(p, p.activeAnimIndex));
    }
}

static bool hasLandingSequence(const PokemonInstance& p) {
    return (p.animLandAIndex >= 0 && p.animLandCIndex >= 0) || (p.animLandBIndex >= 0 && p.animLandCIndex >= 0);
}

static void beginLanding(PokemonInstance& p)
{
    if (!p.usesAirLocomotion) return;

    if (p.airState == AirLocomotionState::LandingStart ||
        p.airState == AirLocomotionState::LandingLoop ||
        p.airState == AirLocomotionState::LandingFinish ||
        p.airState == AirLocomotionState::Grounded)
        return;

    const AirLocomotionState from = p.airState;

    p.airState = AirLocomotionState::LandingStart;
    p.airStateTimeSec = 0.0f;

    int startAnim = -1;
    const char* startRole = "landA";
    if (p.animLandAIndex >= 0) {
        startAnim = p.animLandAIndex;
        startRole = "landA";
    } else if (p.animLandBIndex >= 0) {
        startAnim = p.animLandBIndex;
        startRole = "landB";
        p.airState = AirLocomotionState::LandingLoop;
    } else if (p.animLandIndex >= 0) {
        startAnim = p.animLandIndex;
        startRole = "land";
        p.airState = AirLocomotionState::LandingFinish;
    }

    if (startAnim >= 0) {
        p.activeAnimIndex = startAnim;
        p.animTimeSec = 0.0f;
    }

    const float durA = clipDurationSec(p, p.animLandAIndex);
    const float durB = clipDurationSec(p, p.animLandBIndex);
    const float durC = clipDurationSec(p, p.animLandCIndex);
    p.landingLoopTargetSec = computeLandingLoopTargetAnimSec(p, durA, durB, durC);

    // Match total landing real-time to takeoff real-time (when takeoff exists).
    // This keeps the same A->B->C clip sequence, just sped up as needed.
    p.landAnimSpeedOverride = -1.0f;
    if (p.animTakeoffIndex >= 0) {
        const float takeoffSpeed = std::max(0.05f, p.takeoffAnimSpeed);
        const float takeoffDurAnim = (p.takeoffSec > 0.0f)
            ? std::max(0.05f, p.takeoffSec)
            : std::max(0.12f, clipDurationSec(p, p.animTakeoffIndex));
        const float takeoffReal = (takeoffSpeed > 0.0f) ? (takeoffDurAnim / takeoffSpeed) : takeoffDurAnim;

        float landingTotalAnim = 0.0f;
        const bool legacyOnly = (p.animLandIndex >= 0 && p.animLandAIndex < 0 && p.animLandBIndex < 0 && p.animLandCIndex < 0);
        if (legacyOnly) {
            landingTotalAnim = std::max(0.0f, clipDurationSec(p, p.animLandIndex));
        } else {
            if (p.animLandAIndex >= 0) landingTotalAnim += std::max(0.0f, durA);
            if (p.animLandBIndex >= 0) landingTotalAnim += std::max(0.0f, p.landingLoopTargetSec);
            if (p.animLandCIndex >= 0) landingTotalAnim += std::max(0.0f, durC);
            else if (p.animLandIndex >= 0) landingTotalAnim += std::max(0.0f, clipDurationSec(p, p.animLandIndex));
        }

        if (takeoffReal > 0.05f && landingTotalAnim > 0.05f) {
            p.landAnimSpeedOverride = landingTotalAnim / takeoffReal;
            p.landAnimSpeedOverride = std::max(0.05f, std::min(p.landAnimSpeedOverride, 20.0f));
        }
    }

    if (isDebug(p)) {
        logTransition(p, from, p.airState, 0.0f, std::max(0.05f, (p.landAnimSpeedOverride > 0.0f) ? p.landAnimSpeedOverride : p.landAnimSpeed),
                      0.0f, "beginLanding", p.activeAnimIndex, startRole,
                      clipDurationSec(p, p.activeAnimIndex));
        if (hasLandingSequence(p)) {
            std::cout << "[AnimDebug] " << p.name << " (ID " << p.id << ") landing sequence target:"
                      << " A=" << durA << "s"
                      << " B=" << durB << "s(loopTarget=" << p.landingLoopTargetSec << "s)"
                      << " C=" << durC << "s"
                      << " landingSec=" << p.landingSec
                      << " landAnimSpeed=" << ((p.landAnimSpeedOverride > 0.0f) ? p.landAnimSpeedOverride : p.landAnimSpeed) << " landSpeedOverride=" << p.landAnimSpeedOverride
                      << "\n";
        }
    }
}

void queueAttackAfterLanding(PokemonInstance& p, float attackDurationSec, int attackAnimIndex)
{
    if (!p.usesAirLocomotion) return;


    // Avoid re-queuing every tick; combat may request attacks continuously while in range.
    if (p.pendingAttackAfterLanding) return;
    p.pendingAttackAfterLanding = true;
    p.queuedAttackDurationSec = std::max(0.0f, attackDurationSec);
    p.queuedAttackAnimIndex = attackAnimIndex;

    if (!p.isMoving && p.airState != AirLocomotionState::Grounded) {
        beginLanding(p);
    }
}


// Back-compat overload (defaults to attack1)
void queueAttackAfterLanding(PokemonInstance& p, float attackDurationSec)
{
    queueAttackAfterLanding(p, attackDurationSec, -1);
}

static void startQueuedAttackIfAny(PokemonInstance& p)
{
    if (!p.pendingAttackAfterLanding) return;

    // queuedAttackDurationSec stores the desired REAL time the attack should occupy (usually the move cooldown).
    // We then compute attackAnimSpeed so the attack clip completes in that window.
    const float windowSec = (p.queuedAttackDurationSec > 0.0f) ? p.queuedAttackDurationSec : p.attackDurationSec;

    p.pendingAttackAfterLanding = false;
    p.queuedAttackDurationSec = 0.0f;

    if (windowSec <= 0.0f) return;

    const int animIdx = (p.queuedAttackAnimIndex >= 0) ? p.queuedAttackAnimIndex : p.animAttack1Index;
    p.queuedAttackAnimIndex = -1;

    const float clipDur = clipDurationSec(p, animIdx);
    p.attackAnimSpeed = (clipDur > 0.0f && windowSec > 0.0f) ? (clipDur / windowSec) : 1.0f;

    p.attackTimerSec = windowSec;
    p.animTimeSec = 0.0f;
    p.currentAttackAnimIndex = animIdx;
    p.activeAnimIndex = animIdx;

    if (isDebug(p)) {
        std::cout << "[AnimDebug] " << p.name << " (ID " << p.id << ") starting queued attack"
                  << " anim=" << p.activeAnimIndex
                  << " window=" << windowSec << "s"
                  << " clipDur=" << clipDur << "s"
                  << " speed=" << p.attackAnimSpeed << "x\n";
    }
}


static float computeVisualYOffset(const PokemonInstance& p,
                                        float takeoffElapsedAnimSec,
                                        float takeoffTotalAnimSec,
                                        float landingElapsedAnimSec,
                                        float landingTotalAnimSec)
{
    if (p.airLiftY <= 0.0f) return 0.0f;

    switch (p.airState) {
        case AirLocomotionState::Grounded:
            return 0.0f;

        case AirLocomotionState::TakingOff: {
            const float t = (takeoffTotalAnimSec > 0.0f) ? (takeoffElapsedAnimSec / takeoffTotalAnimSec) : 1.0f;
            return p.airLiftY * smoothstep01(t);
        }

        case AirLocomotionState::Airborne:
            return p.airLiftY;

        case AirLocomotionState::LandingStart:
        case AirLocomotionState::LandingLoop:
        case AirLocomotionState::LandingFinish: {
            const float t = (landingTotalAnimSec > 0.0f) ? (landingElapsedAnimSec / landingTotalAnimSec) : 1.0f;
            return p.airLiftY * (1.0f - smoothstep01(t));
        }
    }

    return 0.0f;
}

void tick(PokemonInstance& p, float dt, float sharedLoopTimeSec)
{
    p.visualYOffset = 0.0f;

    if (!p.usesAirLocomotion) {
        p.airState = AirLocomotionState::Grounded;
        p.airStateTimeSec = 0.0f;
        p.visualYOffset = 0.0f;

        const int desired = p.isMoving ? p.animMoveIndex : p.animIdleIndex;
        if (desired >= 0 && p.activeAnimIndex != desired) p.activeAnimIndex = desired;
        syncLoopTime(p, sharedLoopTimeSec);

        p.wasMovingLastFrame = p.isMoving;
        return;
    }

    const bool startedMoving = p.isMoving && !p.wasMovingLastFrame;
    const bool stoppedMoving = !p.isMoving && p.wasMovingLastFrame;
    p.wasMovingLastFrame = p.isMoving;

    if (startedMoving) beginTakeoff(p);
    if (stoppedMoving) beginLanding(p);

    if (p.pendingAttackAfterLanding && !p.isMoving &&
        (p.airState == AirLocomotionState::Airborne || p.airState == AirLocomotionState::TakingOff))
    {
        beginLanding(p);
    }

    const float takeoffSpeed = std::max(0.05f, p.takeoffAnimSpeed);
    const float landSpeed    = std::max(0.05f, (p.landAnimSpeedOverride > 0.0f) ? p.landAnimSpeedOverride : p.landAnimSpeed);

    const float takeoffDurAnim = (p.takeoffSec > 0.0f)
        ? std::max(0.05f, p.takeoffSec)
        : std::max(0.12f, clipDurationSec(p, p.animTakeoffIndex));

    const float durA = clipDurationSec(p, p.animLandAIndex);
    const float durB = clipDurationSec(p, p.animLandBIndex);
    const float durC = clipDurationSec(p, p.animLandCIndex);
    const float durLandSingle = clipDurationSec(p, p.animLandIndex);
    const float loopTarget = (p.landingLoopTargetSec > 0.0f) ? p.landingLoopTargetSec : computeLandingLoopTargetAnimSec(p, durA, durB, durC);

    float landingTotalAnim = 0.0f;
    if (hasLandingSequence(p)) {
        landingTotalAnim = std::max(0.05f, durA + std::max(0.0f, loopTarget) + durC);
    } else if (durLandSingle > 0.0f) {
        landingTotalAnim = std::max(0.05f, durLandSingle);
    }

    float landingElapsedAnim = 0.0f;

    switch (p.airState) {
        case AirLocomotionState::Grounded: {
            p.visualYOffset = 0.0f;

            if (p.isMoving) {
                beginTakeoff(p);
                if (p.airState != AirLocomotionState::Grounded) break;
            }

            const int desired = p.isMoving ? p.animMoveIndex : p.animGroundIdleIndex;
            if (desired >= 0 && p.activeAnimIndex != desired) p.activeAnimIndex = desired;
            syncLoopTime(p, sharedLoopTimeSec);
        } break;

        case AirLocomotionState::TakingOff: {
            p.airStateTimeSec += dt * takeoffSpeed;

            const int desired = (p.animTakeoffIndex >= 0) ? p.animTakeoffIndex : p.animMoveIndex;
            if (desired >= 0 && p.activeAnimIndex != desired) {
                p.activeAnimIndex = desired;
                p.animTimeSec = 0.0f;
            }

            const float dur = clipDurationSec(p, p.activeAnimIndex);
            if (dur > 0.0f) p.animTimeSec = std::min(p.animTimeSec + dt * takeoffSpeed, dur - 0.0001f);
            else p.animTimeSec += dt * takeoffSpeed;

            p.visualYOffset = computeVisualYOffset(p, p.airStateTimeSec, takeoffDurAnim, 0.0f, 1.0f);

            if (p.airStateTimeSec >= takeoffDurAnim) {
                logTransition(p, AirLocomotionState::TakingOff, AirLocomotionState::Airborne,
                              p.airStateTimeSec, takeoffSpeed, takeoffDurAnim,
                              "takeoffComplete", p.animMoveIndex, "move",
                              clipDurationSec(p, p.animMoveIndex));

                p.airState = AirLocomotionState::Airborne;
                p.airStateTimeSec = 0.0f;
                p.landAnimSpeedOverride = -1.0f;

                const int loop = p.isMoving ? p.animMoveIndex : p.animAirIdleIndex;
                if (loop >= 0) p.activeAnimIndex = loop;
                syncLoopTime(p, sharedLoopTimeSec);

                p.visualYOffset = computeVisualYOffset(p, 1.0f, 1.0f, 0.0f, 1.0f);
            }
        } break;

        case AirLocomotionState::Airborne: {
            p.visualYOffset = (p.airLiftY > 0.0f) ? p.airLiftY : 0.0f;

            const int desired = p.isMoving ? p.animMoveIndex : p.animAirIdleIndex;
            if (desired >= 0 && p.activeAnimIndex != desired) p.activeAnimIndex = desired;
            syncLoopTime(p, sharedLoopTimeSec);
        } break;

        case AirLocomotionState::LandingStart: {
            const float durA_local = (p.animLandAIndex >= 0) ? clipDurationSec(p, p.animLandAIndex) : 0.0f;

            if (p.animLandAIndex < 0 || durA_local <= 0.0f) {
                const AirLocomotionState from = p.airState;
                p.airState = (p.animLandBIndex >= 0) ? AirLocomotionState::LandingLoop : AirLocomotionState::LandingFinish;
                p.airStateTimeSec = 0.0f;

                if (p.airState == AirLocomotionState::LandingLoop && p.animLandBIndex >= 0) {
                    p.activeAnimIndex = p.animLandBIndex;
                    p.animTimeSec = 0.0f;
                    logTransition(p, from, p.airState, 0.0f, landSpeed, 0.0f, "skipA", p.activeAnimIndex, "landB",
                                  clipDurationSec(p, p.activeAnimIndex));
                } else {
                    int fin = (p.animLandCIndex >= 0) ? p.animLandCIndex : p.animLandIndex;
                    p.activeAnimIndex = fin;
                    p.animTimeSec = 0.0f;
                    logTransition(p, from, p.airState, 0.0f, landSpeed, 0.0f, "skipA", p.activeAnimIndex, "landC/land",
                                  clipDurationSec(p, p.activeAnimIndex));
                }
                break;
            }

            if (p.activeAnimIndex != p.animLandAIndex) {
                p.activeAnimIndex = p.animLandAIndex;
                p.animTimeSec = 0.0f;
            }

            p.airStateTimeSec += dt * landSpeed;

            if (durA_local > 0.0f) p.animTimeSec = std::min(p.animTimeSec + dt * landSpeed, durA_local - 0.0001f);
            else p.animTimeSec += dt * landSpeed;

            landingElapsedAnim = std::min(p.airStateTimeSec, durA_local);
            p.visualYOffset = computeVisualYOffset(p, 0.0f, 1.0f, landingElapsedAnim, landingTotalAnim);

            if (p.airStateTimeSec >= durA_local) {
                const AirLocomotionState from = p.airState;
                const bool hasB = (p.animLandBIndex >= 0 && clipDurationSec(p, p.animLandBIndex) > 0.0f);
                p.airState = hasB ? AirLocomotionState::LandingLoop : AirLocomotionState::LandingFinish;

                logTransition(p, from, p.airState, p.airStateTimeSec, landSpeed, durA_local,
                              "landAComplete",
                              hasB ? p.animLandBIndex : ((p.animLandCIndex >= 0) ? p.animLandCIndex : p.animLandIndex),
                              hasB ? "landB" : "landC/land",
                              hasB ? clipDurationSec(p, p.animLandBIndex) : clipDurationSec(p, (p.animLandCIndex>=0)?p.animLandCIndex:p.animLandIndex));

                p.airStateTimeSec = 0.0f;
                if (hasB) {
                    p.activeAnimIndex = p.animLandBIndex;
                    p.animTimeSec = 0.0f;
                } else {
                    p.activeAnimIndex = (p.animLandCIndex >= 0) ? p.animLandCIndex : p.animLandIndex;
                    p.animTimeSec = 0.0f;
                }
            }
        } break;

        case AirLocomotionState::LandingLoop: {
            const float durB_local = clipDurationSec(p, p.animLandBIndex);

            if (p.animLandBIndex < 0 || durB_local <= 0.0f) {
                const AirLocomotionState from = p.airState;
                p.airState = AirLocomotionState::LandingFinish;
                p.airStateTimeSec = 0.0f;
                p.activeAnimIndex = (p.animLandCIndex >= 0) ? p.animLandCIndex : p.animLandIndex;
                p.animTimeSec = 0.0f;
                logTransition(p, from, p.airState, 0.0f, landSpeed, 0.0f, "missingB",
                              p.activeAnimIndex, "landC/land",
                              clipDurationSec(p, p.activeAnimIndex));
                break;
            }

            if (p.activeAnimIndex != p.animLandBIndex) {
                p.activeAnimIndex = p.animLandBIndex;
                p.animTimeSec = 0.0f;
            }

            if (p.landingLoopTargetSec <= 0.0f) {
                p.landingLoopTargetSec = loopTarget;
            }

            p.airStateTimeSec += dt * landSpeed;

            if (durB_local > 0.0f) {
                p.animTimeSec = std::fmod(p.animTimeSec + dt * landSpeed, durB_local);
            } else {
                p.animTimeSec += dt * landSpeed;
            }

            landingElapsedAnim = std::max(0.0f, durA) + std::min(p.airStateTimeSec, std::max(0.0f, p.landingLoopTargetSec));
            p.visualYOffset = computeVisualYOffset(p, 0.0f, 1.0f, landingElapsedAnim, landingTotalAnim);

            if (p.airStateTimeSec >= p.landingLoopTargetSec) {
                const AirLocomotionState from = p.airState;
                p.airState = AirLocomotionState::LandingFinish;

                const float target = std::max(0.0f, p.landingLoopTargetSec);
                logTransition(p, from, p.airState, p.airStateTimeSec, landSpeed, target,
                              "landBComplete",
                              (p.animLandCIndex >= 0) ? p.animLandCIndex : p.animLandIndex,
                              "landC/land",
                              clipDurationSec(p, (p.animLandCIndex>=0)?p.animLandCIndex:p.animLandIndex));

                p.airStateTimeSec = 0.0f;
                p.activeAnimIndex = (p.animLandCIndex >= 0) ? p.animLandCIndex : p.animLandIndex;
                p.animTimeSec = 0.0f;
            }
        } break;

        case AirLocomotionState::LandingFinish: {
            int fin = -1;
            if (p.animLandCIndex >= 0) fin = p.animLandCIndex;
            else if (p.animLandIndex >= 0) fin = p.animLandIndex;

            if (fin < 0) {
                const AirLocomotionState from = p.airState;
                p.airState = AirLocomotionState::Grounded;
                p.airStateTimeSec = 0.0f;
                p.landAnimSpeedOverride = -1.0f;
                p.visualYOffset = 0.0f;

                logTransition(p, from, p.airState, 0.0f, landSpeed, 0.0f, "missingLandClip",
                              p.animGroundIdleIndex, "ground_idle",
                              clipDurationSec(p, p.animGroundIdleIndex));

                startQueuedAttackIfAny(p);
                return;
            }

            const float durFin = clipDurationSec(p, fin);
            if (p.activeAnimIndex != fin) {
                p.activeAnimIndex = fin;
                p.animTimeSec = 0.0f;
            }

            p.airStateTimeSec += dt * landSpeed;

            if (durFin > 0.0f) p.animTimeSec = std::min(p.animTimeSec + dt * landSpeed, durFin - 0.0001f);
            else p.animTimeSec += dt * landSpeed;

            landingElapsedAnim = std::max(0.0f, durA) + std::max(0.0f, loopTarget) + std::min(p.airStateTimeSec, std::max(0.0f, durFin));
            p.visualYOffset = computeVisualYOffset(p, 0.0f, 1.0f, landingElapsedAnim, landingTotalAnim);

            if (durFin <= 0.0f || p.airStateTimeSec >= durFin) {
                const AirLocomotionState from = p.airState;
                logTransition(p, from, AirLocomotionState::Grounded, p.airStateTimeSec, landSpeed, durFin,
                              "landingComplete",
                              p.animGroundIdleIndex, "ground_idle",
                              clipDurationSec(p, p.animGroundIdleIndex));

                p.airState = AirLocomotionState::Grounded;
                p.airStateTimeSec = 0.0f;
                p.landAnimSpeedOverride = -1.0f;
                p.visualYOffset = 0.0f;

                startQueuedAttackIfAny(p);
                if (p.attackTimerSec > 0.0f) return;

                const int loop = p.isMoving ? p.animMoveIndex : p.animGroundIdleIndex;
                if (loop >= 0) p.activeAnimIndex = loop;
                syncLoopTime(p, sharedLoopTimeSec);
            }
        } break;
    }
}

} // namespace FlightLocomotion
