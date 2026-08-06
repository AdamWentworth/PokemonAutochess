// tests/TestMovementSystem.cpp
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "engine/core/Paths.h"
#include "engine/core/Random.h"
#include "engine/core/TimeSources.h"
#include "engine/core/Services.h"
#include "engine/core/ecs/World.h"

#include "game/GameConfig.h"
#include "game/GameServices.h"
#include "game/GameWorld.h"
#include "game/PokemonInstance.h"
#include "game/PhaseState.h"
#include "game/assets/DevAssetStore.h"
#include "game/animation/FlightLocomotion.h"
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

    engine::CoreServices core;
    core.rng = &services.rng;
    core.time = &services.time;

    engine::ecs::World ecsWorld(&core);
    engine::ecs::Entity combatEntity = ecsWorld.create();
    ecsWorld.add<game::CombatActive>(combatEntity, game::CombatActive{true});

    MovementSystem movement(&world, services, combatEntity);

    std::unordered_map<int, glm::ivec2> originalCells;
    for (const auto& u : units) {
        originalCells[u.id] = worldToGrid(cfg, u.position);
    }

    movement.update(ecsWorld, 0.01f);

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

    GameWorld chainWorld(cfg);
    chainWorld.setData(&db);
    chainWorld.setLogger(&log);

    auto& chainUnits = chainWorld.getPokemons();
    chainUnits.push_back(makeUnit(cfg, "runner", PokemonSide::Player, 1, 1));
    chainUnits.push_back(makeUnit(cfg, "target", PokemonSide::Enemy, 6, 6));

    MovementSystem chainMovement(&chainWorld, services, combatEntity);
    const glm::ivec2 startCell = worldToGrid(cfg, chainUnits[0].position);

    chainMovement.update(ecsWorld, 2.0f);

    const PokemonInstance& runnerAfterArrival = chainUnits[0];
    const glm::ivec2 arrivedCell = worldToGrid(cfg, runnerAfterArrival.position);
    if (!runnerAfterArrival.isMoving) {
        outFail = "Units should keep locomotion active across chained movement steps until replanning decides to stop.";
        return false;
    }
    if (runnerAfterArrival.committedDest.x != -1 || runnerAfterArrival.committedDest.y != -1) {
        outFail = "Arrived chained movement step should clear committedDest while waiting for the next hop.";
        return false;
    }
    if (arrivedCell.x == startCell.x && arrivedCell.y == startCell.y) {
        outFail = "Chained movement test did not advance the runner to a new cell.";
        return false;
    }

    chainMovement.update(ecsWorld, 0.01f);

    const PokemonInstance& runnerContinuing = chainUnits[0];
    if (!runnerContinuing.isMoving ||
        runnerContinuing.committedDest.x < 0 ||
        runnerContinuing.committedDest.y < 0) {
        outFail = "Chained movement should immediately acquire the next committed step on the following planner tick.";
        return false;
    }
    if (runnerContinuing.committedDest.x == arrivedCell.x &&
        runnerContinuing.committedDest.y == arrivedCell.y) {
        outFail = "Chained movement should commit beyond the cell that was just reached.";
        return false;
    }

    GameWorld flightWorld(cfg);
    flightWorld.setData(&db);
    flightWorld.setLogger(&log);
    auto& flightUnits = flightWorld.getPokemons();
    flightUnits.push_back(makeUnit(cfg, "pidgey", PokemonSide::Player, 1, 1));
    flightUnits.push_back(makeUnit(cfg, "flight_target", PokemonSide::Enemy, 6, 6));

    PokemonInstance& flightBird = flightUnits[0];
    flightBird.usesAirLocomotion = true;
    flightBird.airLiftY = 0.65f;
    flightBird.takeoffAnimSpeed = 1.0f;
    flightBird.animGroundIdleIndex = 0;
    flightBird.animAirIdleIndex = 1;
    flightBird.animTakeoffIndex = 2;
    flightBird.animTakeoffLoopIndex = 3;
    flightBird.animMoveIndex = 4;
    flightBird.backendAnimDurationsSec = {1.6f, 1.0f, 0.1f, 0.3f, 0.55f};

    MovementSystem flightMovement(&flightWorld, services, combatEntity);
    const glm::vec3 takeoffOrigin = flightBird.position;
    flightMovement.update(ecsWorld, 0.10f);
    if (!flightBird.isMoving || flightBird.committedDest.x < 0 ||
        glm::length(glm::vec2(flightBird.position.x - takeoffOrigin.x,
                              flightBird.position.z - takeoffOrigin.z)) > 1e-5f) {
        outFail = "A grounded flyer must reserve its movement step without translating before takeoff.";
        return false;
    }

    FlightLocomotion::tick(flightBird, 0.01f, 0.0f);
    flightMovement.update(ecsWorld, 0.10f);
    if (flightBird.airState != AirLocomotionState::TakingOff ||
        glm::length(glm::vec2(flightBird.position.x - takeoffOrigin.x,
                              flightBird.position.z - takeoffOrigin.z)) > 1e-5f) {
        outFail = "A flyer must remain over its takeoff origin until the ascent chain completes.";
        return false;
    }

    for (int step = 0;
         step < 20 && flightBird.airState != AirLocomotionState::Airborne;
         ++step) {
        FlightLocomotion::tick(flightBird, 0.05f, step * 0.05f);
    }
    if (flightBird.airState != AirLocomotionState::Airborne) {
        outFail = "Takeoff gating test did not reach the airborne state.";
        return false;
    }

    const glm::vec3 airborneOrigin = flightBird.position;
    flightMovement.update(ecsWorld, 0.10f);
    if (glm::length(glm::vec2(flightBird.position.x - airborneOrigin.x,
                              flightBird.position.z - airborneOrigin.z)) <= 1e-5f) {
        outFail = "An airborne flyer should begin translating toward its reserved destination.";
        return false;
    }

    PokemonInstance flyer;
    flyer.name = "pidgey";
    flyer.usesAirLocomotion = true;
    flyer.airLiftY = 0.65f;
    flyer.takeoffAnimSpeed = 1.0f;
    flyer.animGroundIdleIndex = 0;
    flyer.animAirIdleIndex = 1;
    flyer.animTakeoffIndex = 2;
    flyer.animTakeoffLoopIndex = 3;
    flyer.animMoveIndex = 4;
    flyer.animLandAIndex = 5;
    flyer.animLandBIndex = 6;
    flyer.animLandCIndex = 7;
    flyer.animAttack1Index = 8;
    flyer.backendAnimDurationsSec = {
        1.6f, 1.0f, 0.1f, 0.3f, 0.55f, 0.4f, 0.5f, 0.8f, 1.0f};
    flyer.isMoving = true;
    flyer.wasMovingLastFrame = false;
    FlightLocomotion::tick(flyer, 0.01f, 0.0f);
    if (flyer.airState != AirLocomotionState::TakingOff ||
        flyer.activeAnimIndex != flyer.animTakeoffIndex) {
        outFail = "Pidgey-style movement should begin with its takeoff role.";
        return false;
    }
    FlightLocomotion::tick(flyer, 0.10f, 0.10f);
    if (flyer.airState != AirLocomotionState::TakingOff ||
        flyer.activeAnimIndex != flyer.animTakeoffLoopIndex) {
        outFail = "Pidgey-style takeoff must advance from jumpup-start into jumpup-loop before flight.";
        return false;
    }
    for (int step = 0;
         step < 20 && flyer.airState != AirLocomotionState::Airborne;
         ++step) {
        FlightLocomotion::tick(flyer, 0.05f, step * 0.05f);
    }
    if (flyer.airState != AirLocomotionState::Airborne ||
        flyer.activeAnimIndex != flyer.animMoveIndex) {
        outFail = "Pidgey-style movement should fly with its airborne move role after takeoff.";
        return false;
    }

    FlightLocomotion::queueAttackAfterLanding(flyer, 0.75f, flyer.animAttack1Index);
    flyer.isMoving = false;
    bool attackedBeforeLanding = false;
    for (int step = 0;
         step < 100 && flyer.attackTimerSec <= 0.0f;
         ++step) {
        FlightLocomotion::tick(flyer, 0.02f, step * 0.02f);
        if (flyer.airState != AirLocomotionState::Grounded &&
            flyer.attackTimerSec > 0.0f) {
            attackedBeforeLanding = true;
        }
    }
    if (attackedBeforeLanding ||
        flyer.airState != AirLocomotionState::Grounded ||
        flyer.pendingAttackAfterLanding ||
        flyer.attackTimerSec <= 0.0f ||
        flyer.activeAnimIndex != flyer.animAttack1Index) {
        outFail = "Pidgey-style movement must finish landing before starting its queued attack.";
        return false;
    }

    return true;
}
