// tests/TestMovementSystem.cpp
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "engine/core/Paths.h"
#include "engine/core/Random.h"
#include "engine/core/TimeSources.h"

#include "game/GameConfig.h"
#include "game/GameServices.h"
#include "game/GameWorld.h"
#include "game/PokemonInstance.h"
#include "game/assets/DevAssetStore.h"
#include "game/config/GameDataDb.h"
#include "game/logging/LogBus.h"
#include "game/scripting/ScriptEventBus.h"
#include "game/systems/MovementSystem.h"
#include "game/scripting/LuaBindings_Internal.h"

namespace {
PokemonInstance makeUnit(const GameConfigData& cfg,
                         const std::string& name,
                         PokemonSide side,
                         int col,
                         int row,
                         float speed = 1.0f) {
    PokemonInstance u;
    u.id = PokemonInstance::getNextUnitID();
    u.name = name;
    u.side = side;
    u.alive = true;
    u.movementSpeed = speed;
    u.position = gridToWorld(cfg, col, row);
    u.hp = 100;
    u.maxHP = 100;
    u.attack = 10;
    u.energy = 0;
    u.maxEnergy = 100;
    u.isMoving = false;
    u.moveT = 1.0f;
    u.committedDest = {-1, -1};
    return u;
}

int64_t cellKey(int col, int row) {
    return (static_cast<int64_t>(row) << 32) | static_cast<uint32_t>(col);
}
} // namespace

bool test_movement_invariants(std::string& outFail) {
    GameConfigData cfg;
    GameDataDb db;
    LogBus::Logger log;
    log.setEchoToStdout(false);
    log.setFeedEnabled(false);

    ScriptEventBus events;
    game::assets::DevAssetStore assets(engine::paths::dataRoot());
    engine::XorShift32 rng(1u);
    engine::ManualTimeSource time;

    GameServices services(cfg, db, log, events, assets, rng, time);
    GameWorld world(cfg);
    world.setData(&db);
    world.setLogger(&log);

    auto& units = world.getPokemons();
    units.push_back(makeUnit(cfg, "unit_a", PokemonSide::Player, 1, 1));
    units.push_back(makeUnit(cfg, "unit_b", PokemonSide::Player, 1, 3));
    units.push_back(makeUnit(cfg, "unit_c", PokemonSide::Enemy, 6, 6));

    MovementSystem movement(&world, services);

    std::unordered_map<int, glm::ivec2> originalCells;
    for (const auto& u : units) {
        originalCells[u.id] = worldToGrid(cfg, u.position);
    }

    movement.update(0.01f);

    std::unordered_set<int64_t> committed;
    int movingCount = 0;

    for (const auto& u : units) {
        if (!u.alive) continue;
        if (!u.isMoving) continue;

        ++movingCount;

        if (u.committedDest.x < 0 || u.committedDest.y < 0) {
            outFail = "Unit marked moving without committed destination.";
            return false;
        }

        if (u.committedDest.x < 0 || u.committedDest.x >= cfg.cols ||
            u.committedDest.y < 0 || u.committedDest.y >= cfg.rows) {
            outFail = "Committed destination out of bounds.";
            return false;
        }

        const auto it = originalCells.find(u.id);
        if (it == originalCells.end()) {
            outFail = "Missing original cell tracking.";
            return false;
        }

        const int dx = std::abs(u.committedDest.x - it->second.x);
        const int dy = std::abs(u.committedDest.y - it->second.y);
        if (dx > 1 || dy > 1 || (dx == 0 && dy == 0)) {
            outFail = "Committed move is not a single-cell step.";
            return false;
        }

        const int64_t k = cellKey(u.committedDest.x, u.committedDest.y);
        if (committed.count(k) != 0) {
            outFail = "Multiple units committed to the same destination cell.";
            return false;
        }
        committed.insert(k);

        for (const auto& [id, cell] : originalCells) {
            if (id == u.id) continue;
            if (cell.x == u.committedDest.x && cell.y == u.committedDest.y) {
                outFail = "Unit committed into an occupied cell.";
                return false;
            }
        }
    }

    if (movingCount == 0) {
        outFail = "No units committed to move; test setup invalid.";
        return false;
    }

    return true;
}
