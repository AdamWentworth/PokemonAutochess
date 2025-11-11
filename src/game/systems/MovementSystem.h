// MovementSystem.h
#pragma once
#include "../../engine/core/IUpdatable.h"
#include "../GameWorld.h"
#include <sol/sol.hpp>

// REMOVE this (causes IntelliSense grief):
// #include "../../GridOccupancy.h"

// Forward-declare instead:
class GridOccupancy;

class MovementSystem : public IUpdatable {
public:
    explicit MovementSystem(GameWorld* world);
    MovementSystem(GameWorld* world, const GridOccupancy& /*unused*/); // compat

    void update(float deltaTime) override;

private:
    GameWorld* gameWorld;
    sol::state lua;
    bool ok = false;

    static constexpr float CELL_SIZE = 1.2f;
    static constexpr int GRID_COLS = 8;
    static constexpr int GRID_ROWS = 8;

    void exposeConstants();
    void loadScript();
};
