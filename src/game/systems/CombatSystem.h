// CombatSystem.h
#pragma once
#include "../../engine/core/IUpdatable.h"
#include <sol/sol.hpp>

class GameWorld;

class CombatSystem : public IUpdatable {
public:
    explicit CombatSystem(GameWorld* world);
    void update(float deltaTime) override;

private:
    GameWorld* gameWorld;
    sol::state lua;
    bool ok = false;

    void loadScript();
};
