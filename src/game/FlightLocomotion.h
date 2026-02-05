// src/game/FlightLocomotion.h
#pragma once

#include "game/PokemonInstance.h"

// Visual-only airborne locomotion helper.
// This does NOT change gameplay timing; it only drives PokemonInstance::visualYOffset
// and selects takeoff/air/landing animation clips for small fliers.

namespace FlightLocomotion {

bool isAirborne(const PokemonInstance& p);

// Queue a single attack cycle to start once the unit finishes landing.
// (Used to avoid "ghost hits" during takeoff/landing visual sequences.)
void queueAttackAfterLanding(PokemonInstance& p, float attackDurationSec, int attackAnimIndex);

// Back-compat overload (defaults to attack1).
void queueAttackAfterLanding(PokemonInstance& p, float attackDurationSec);

// Tick the flight locomotion state machine for this unit.
// sharedLoopTimeSec is used to sync loop animations across units for stability.
void tick(PokemonInstance& p, float dt, float sharedLoopTimeSec);

} // namespace FlightLocomotion
