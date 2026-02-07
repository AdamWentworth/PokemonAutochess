// src/game/ecs/RoundState.h
#pragma once

#include "game/systems/RoundPhase.h"

namespace game {

struct RoundState {
    RoundPhase phase = RoundPhase::Planning;
};

} // namespace game
