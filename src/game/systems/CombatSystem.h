// CombatSystem.h
#pragma once
#include "engine/core/Updatable.h"
#include <memory>
#include <sol/sol.hpp>

class GameWorld;
class ScriptAPI;
class ScriptEventBus;

class CombatSystem : public Updatable {
public:
    explicit CombatSystem(GameWorld* world, ScriptEventBus* events = nullptr);
    void update(float deltaTime) override;

private:
    GameWorld* gameWorld;
    sol::state lua;
    std::unique_ptr<ScriptAPI> api;
    bool ok = false;

    void loadScript();
};