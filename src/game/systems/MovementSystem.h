// MovementSystem.h
#pragma once
#include "engine/core/ecs/ISystem.h"
#include "engine/core/ecs/Entity.h"
#include "game/GameServices.h"
#include "game/GameWorld.h"

class MovementSystem : public engine::ecs::ISystem {
public:
    explicit MovementSystem(GameWorld* world, GameServices& services, engine::ecs::Entity combatEntity);
    ~MovementSystem();

    const char* debugName() const override { return "movement"; }
    void update(engine::ecs::World& world, float deltaTime) override;

private:
    GameWorld* gameWorld;
    GameServices& services;
    engine::ecs::Entity combatEntity;
};
