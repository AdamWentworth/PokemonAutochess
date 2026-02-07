// MovementSystem.h
#pragma once
#include "engine/core/Updatable.h"
#include "game/GameServices.h"
#include "game/GameWorld.h"
#include <memory>
#include <sol/sol.hpp>

// Forward-declare instead:
class ScriptAPI;

class MovementSystem : public Updatable {
public:
    explicit MovementSystem(GameWorld* world, GameServices& services);

    void update(float deltaTime) override;

private:
    GameWorld* gameWorld;
    GameServices& services;
    sol::state lua;
    std::unique_ptr<ScriptAPI> api;
    bool ok = false;

    static constexpr float CELL_SIZE = 1.2f;
    static constexpr int GRID_COLS = 8;
    static constexpr int GRID_ROWS = 8;

    void exposeConstants();
    void loadScript();
};
