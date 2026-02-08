// CombatSystem.h
#pragma once
#include "engine/core/ecs/ISystem.h"
#include "engine/core/ecs/Entity.h"
#include "game/GameServices.h"
#include <memory>
#include <sol/sol.hpp>

class GameWorld;
class ScriptAPI;

class CombatSystem : public engine::ecs::ISystem {
public:
    explicit CombatSystem(GameWorld* world, GameServices& services, engine::ecs::Entity combatEntity);
    ~CombatSystem();
    void update(engine::ecs::World& world, float deltaTime) override;

private:
    GameWorld* gameWorld;
    GameServices& services;
    engine::ecs::Entity combatEntity;
    sol::state lua;
    std::unique_ptr<ScriptAPI> api;
    bool ok = false;

    void loadScript();
};
