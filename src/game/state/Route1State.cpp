// src/game/state/Route1State.cpp
#include "Route1State.h"

Route1State::Route1State(GameStateManager* manager, GameWorld* world)
    : CombatState(manager, world, "scripts/states/route1.lua") {}

Route1State::~Route1State() = default;