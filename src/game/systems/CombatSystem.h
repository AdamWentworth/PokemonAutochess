// CombatSystem.h
#pragma once
#include "engine/core/Updatable.h"
#include "game/GameServices.h"
#include <memory>
#include <sol/sol.hpp>

class GameWorld;
class ScriptAPI;

class CombatSystem : public Updatable {
public:
    explicit CombatSystem(GameWorld* world, GameServices& services);
    ~CombatSystem();
    void update(float deltaTime) override;

private:
    GameWorld* gameWorld;
    GameServices& services;
    sol::state lua;
    std::unique_ptr<ScriptAPI> api;
    bool ok = false;

    void loadScript();
};
