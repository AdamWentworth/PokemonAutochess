#pragma once

#include "game/PokemonInstance.h"
#include <cmath>

namespace game::unit_facing {

inline bool faceDirection(PokemonInstance& unit, const glm::vec3& direction) {
    const float lengthSquared = direction.x * direction.x + direction.z * direction.z;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1e-8f) return false;
    unit.rotation.y = glm::degrees(std::atan2(direction.x, direction.z));
    return true;
}

inline bool hasTravelFacing(const PokemonInstance& unit) {
    // isMoving also keeps locomotion continuous between committed steps.
    return unit.isMoving || unit.ledgeJump.active();
}

inline bool faceTravel(PokemonInstance& unit) {
    if (!hasTravelFacing(unit)) return false;
    // The full segment stays valid at arrival and throughout jump landing.
    // Height affects the trajectory, never the horizontal facing direction.
    return faceDirection(unit, unit.moveTo - unit.moveFrom);
}

inline bool faceTarget(PokemonInstance& unit, const glm::vec3& targetPosition) {
    if (hasTravelFacing(unit)) return faceTravel(unit);
    return faceDirection(unit, targetPosition - unit.position);
}

} // namespace game::unit_facing
