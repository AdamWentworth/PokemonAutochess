#pragma once

#include "game/systems/RoundPhase.h"

namespace game {

struct CombatActive {
    bool active = false;
};

struct RoundState {
    RoundPhase phase = RoundPhase::Planning;
};

} // namespace game
