// CombatSystem.h
#pragma once
#include "engine/core/Updatable.h"
#include <sol/sol.hpp>

class GameWorld;

class CombatSystem : public Updatable {
public:
    explicit CombatSystem(GameWorld* world);
    void update(float deltaTime) override;

private:
    GameWorld* gameWorld;
    sol::state lua;
    bool ok = false;

    void loadScript();
};
