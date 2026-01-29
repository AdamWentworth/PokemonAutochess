// src/game/FlightLocomotion.h
#pragma once

#include <algorithm>
#include <cmath>

#include "game/PokemonInstance.h"
#include "engine/render/Model.h"

// Visual-only airborne locomotion helper.
// Goal: add a short "takeoff" and "landing" animation window so small fliers (e.g. Pidgey)
// don't instantly snap between grounded idle and flying locomotion.
//
// IMPORTANT: This does NOT change gameplay timing. It only changes which animation is shown
// and applies a render-time Y offset (PokemonInstance::visualYOffset).

namespace FlightLocomotion {

static inline float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
static inline float easeOutSine(float t01) { return std::sin((t01 * 3.14159265f) * 0.5f); }
static inline float easeInSine (float t01) { return 1.0f - std::cos((t01 * 3.14159265f) * 0.5f); }

static inline float clipDurationSec(const PokemonInstance& p, int animIndex)
{
    if (!p.model || animIndex < 0) return 0.0f;
    return p.model->getAnimationDurationSec(animIndex);
}

static inline bool flightEnabled(const PokemonInstance& p)
{
    // Hard gate: we only apply any Y offset when BOTH takeoff and land one-shots exist.
    // This prevents grounded Pokémon (e.g., Charmander) from ever being lifted by accident.
    return p.usesAirLocomotion && p.animTakeoffIndex >= 0 && p.animLandIndex >= 0 && p.flightHeight > 0.0f;
}

static inline bool isAirborne(const PokemonInstance& p)
{
    return flightEnabled(p) && p.airState != AirLocomotionState::Grounded;
}

static inline void beginTakeoff(PokemonInstance& p)
{
    if (!flightEnabled(p)) return;
    if (p.airState == AirLocomotionState::TakingOff || p.airState == AirLocomotionState::Airborne) return;

    p.airState = AirLocomotionState::TakingOff;
    p.airStateTimeSec = 0.0f;
    p.visualYOffset = 0.0f;

    // Start the one-shot at t=0.
    if (p.animTakeoffIndex >= 0) {
        p.activeAnimIndex = p.animTakeoffIndex;
        p.animTimeSec = 0.0f;
    }
}

static inline void beginLanding(PokemonInstance& p)
{
    if (!flightEnabled(p)) return;
    if (p.airState == AirLocomotionState::Landing || p.airState == AirLocomotionState::Grounded) return;

    p.airState = AirLocomotionState::Landing;
    p.airStateTimeSec = 0.0f;

    if (p.animLandIndex >= 0) {
        p.activeAnimIndex = p.animLandIndex;
        p.animTimeSec = 0.0f;
    }
}

static inline void queueAttackAfterLanding(PokemonInstance& p, float attackDurationSec)
{
    if (!flightEnabled(p)) return;

    p.pendingAttackAfterLanding = true;
    p.queuedAttackDurationSec = std::max(0.0f, attackDurationSec);

    // If we are currently airborne and not moving, start landing immediately.
    if (!p.isMoving && p.airState != AirLocomotionState::Grounded) {
        beginLanding(p);
    }
}

static inline void startQueuedAttackIfAny(PokemonInstance& p)
{
    if (!p.pendingAttackAfterLanding) return;

    const float dur = (p.queuedAttackDurationSec > 0.0f) ? p.queuedAttackDurationSec : p.attackDurationSec;
    p.pendingAttackAfterLanding = false;
    p.queuedAttackDurationSec = 0.0f;

    if (dur <= 0.0f) return;

    p.attackTimerSec = dur;
    p.animTimeSec = 0.0f;
    p.activeAnimIndex = p.animAttack1Index;
}

static inline void syncLoopTime(PokemonInstance& p, float sharedLoopTimeSec)
{
    if (!p.model || p.activeAnimIndex < 0) {
        p.animTimeSec = sharedLoopTimeSec;
        return;
    }

    const float dur = p.model->getAnimationDurationSec(p.activeAnimIndex);
    if (dur > 0.0f) {
        p.animTimeSec = std::fmod(sharedLoopTimeSec, dur);
    } else {
        p.animTimeSec = sharedLoopTimeSec;
    }
}

// Update airborne visuals and select a locomotion animation.
// Should only be called when the unit is NOT currently playing the attack one-shot.
static inline void tick(PokemonInstance& p, float dt, float sharedLoopTimeSec)
{
    // Non-fliers (and any unit missing takeoff+land one-shots) behave exactly like before.
    if (!flightEnabled(p)) {
        p.airState = AirLocomotionState::Grounded;
        p.airStateTimeSec = 0.0f;
        p.visualYOffset = 0.0f;

        p.pendingAttackAfterLanding = false;
        p.queuedAttackDurationSec = 0.0f;

        const int desired = p.isMoving ? p.animMoveIndex : p.animIdleIndex;
        if (desired >= 0 && p.activeAnimIndex != desired) p.activeAnimIndex = desired;
        syncLoopTime(p, sharedLoopTimeSec);

        p.wasMovingLastFrame = p.isMoving;
        return;
    }

    // Detect movement edges for takeoff/landing.
    const bool startedMoving = p.isMoving && !p.wasMovingLastFrame;
    const bool stoppedMoving = !p.isMoving && p.wasMovingLastFrame;
    p.wasMovingLastFrame = p.isMoving;

    if (startedMoving) beginTakeoff(p);
    if (stoppedMoving) beginLanding(p);

    // If an attack is queued and we're still airborne (but not moving), land first.
    if (p.pendingAttackAfterLanding && !p.isMoving && p.airState == AirLocomotionState::Airborne) {
        beginLanding(p);
    }

    const float takeoffDur = (p.takeoffSec > 0.0f)
        ? std::max(0.05f, p.takeoffSec)
        : std::max(0.12f, clipDurationSec(p, p.animTakeoffIndex));

    const float landingDur = (p.landingSec > 0.0f)
        ? std::max(0.05f, p.landingSec)
        : std::max(0.10f, clipDurationSec(p, p.animLandIndex));

    switch (p.airState) {
        case AirLocomotionState::Grounded: {
            p.visualYOffset = 0.0f;

            // If movement toggled on without a detectable edge, still take off.
            if (p.isMoving) {
                beginTakeoff(p);
                if (p.airState != AirLocomotionState::Grounded) break;
            }

            const int desired = p.isMoving ? p.animMoveIndex : p.animGroundIdleIndex;
            if (desired >= 0 && p.activeAnimIndex != desired) p.activeAnimIndex = desired;
            syncLoopTime(p, sharedLoopTimeSec);
        } break;

        case AirLocomotionState::TakingOff: {
            const float speed = std::max(0.05f, p.takeoffAnimSpeed);
            p.airStateTimeSec += dt * speed;

            const float t01 = clamp01(p.airStateTimeSec / takeoffDur);
            p.visualYOffset = easeOutSine(t01) * p.flightHeight;

            // Play takeoff one-shot if available, otherwise just use move.
            const int desired = (p.animTakeoffIndex >= 0) ? p.animTakeoffIndex : p.animMoveIndex;
            if (desired >= 0 && p.activeAnimIndex != desired) {
                p.activeAnimIndex = desired;
                p.animTimeSec = 0.0f;
            }

            // Advance one-shot time (clamp to last frame)
            const float dur = clipDurationSec(p, p.activeAnimIndex);
            if (dur > 0.0f) p.animTimeSec = std::min(p.animTimeSec + dt * speed, dur - 0.0001f);
            else p.animTimeSec += dt * speed;

            if (p.airStateTimeSec >= takeoffDur) {
                p.airState = AirLocomotionState::Airborne;
                p.airStateTimeSec = 0.0f;

                const int loop = p.isMoving ? p.animMoveIndex : p.animAirIdleIndex;
                if (loop >= 0) p.activeAnimIndex = loop;
                syncLoopTime(p, sharedLoopTimeSec);
            }
        } break;

        case AirLocomotionState::Airborne: {
            // Subtle bob so hovering isn't rigid.
            const float bob = 0.04f * std::sin(sharedLoopTimeSec * 4.0f);
            p.visualYOffset = p.flightHeight + bob;

            const int desired = p.isMoving ? p.animMoveIndex : p.animAirIdleIndex;
            if (desired >= 0 && p.activeAnimIndex != desired) p.activeAnimIndex = desired;
            syncLoopTime(p, sharedLoopTimeSec);
        } break;

        case AirLocomotionState::Landing: {
            const float speed = std::max(0.05f, p.landAnimSpeed);
            p.airStateTimeSec += dt * speed;

            const float t01 = clamp01(p.airStateTimeSec / landingDur);
            p.visualYOffset = (1.0f - easeInSine(t01)) * p.flightHeight;

            const int desired = (p.animLandIndex >= 0) ? p.animLandIndex : p.animGroundIdleIndex;
            if (desired >= 0 && p.activeAnimIndex != desired) {
                p.activeAnimIndex = desired;
                p.animTimeSec = 0.0f;
            }

            const float dur = clipDurationSec(p, p.activeAnimIndex);
            if (dur > 0.0f) p.animTimeSec = std::min(p.animTimeSec + dt * speed, dur - 0.0001f);
            else p.animTimeSec += dt * speed;

            if (p.airStateTimeSec >= landingDur) {
                p.airState = AirLocomotionState::Grounded;
                p.airStateTimeSec = 0.0f;
                p.visualYOffset = 0.0f;

                // If an attack was queued, start it now (still this frame).
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
