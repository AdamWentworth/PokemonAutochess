// src/game/state/Route1State.h
#pragma once
#include "CombatState.h"

class Route1State : public CombatState {
public:
    Route1State(GameStateManager* manager, GameWorld* world);
    ~Route1State() override;
};