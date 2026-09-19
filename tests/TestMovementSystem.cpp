// tests/TestMovementSystem.cpp
#include <cmath>
#include <algorithm>
#include <vector>
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
#include "game/arena/AuthoredCombatMap.h"
#include "game/config/AnimSetLoader.h"
#include "game/config/GameDataDb.h"
#include "game/logging/LogBus.h"
#include "game/scripting/ScriptEventBus.h"
#include "game/scripting/ScriptAPI.h"
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

bool facesDirection(const PokemonInstance &unit, const glm::vec3 &delta) {
    const glm::vec2 motion(delta.x, delta.z);
    if (glm::length(motion) <= 1e-5f) return true;
    const float yaw = glm::radians(unit.rotation.y);
    return glm::dot(glm::vec2(std::sin(yaw), std::cos(yaw)), glm::normalize(motion)) > .9999f;
}
} // namespace

bool test_ledge_jump_movement(std::string &outFail) {
    GameConfigData cfg;
    GameDataDb db;
    LogBus::Logger log;
    log.setEchoToStdout(false);
    ScriptEventBus events;
    game::assets::DevAssetStore assets(engine::paths::dataRoot());
    engine::XorShift32 rng(1u);
    engine::ManualTimeSource time;
    GameServices services(cfg, db, log, events, assets, rng, time);
    engine::CoreServices core;
    core.rng = &rng;
    core.time = &time;
    engine::ecs::World ecs(&core);
    const auto combat = ecs.create();
    ecs.add<game::CombatActive>(combat, game::CombatActive{true});
    game::arena::ArenaMapData data;
    for (int z = 0; z < cfg.rows; ++z)
        for (int x = 0; x < cfg.cols; ++x) {
            data.tiles[{x, z}] = {x, z, z <= 1 ? 1 : 0, 0, 0};
            data.playableCells.insert({x, z});
        }
    const auto rules = std::make_shared<game::arena::AuthoredCombatMap>(data, game::arena::Cell{0, 0});
    for (float dt : {1.0f / 120, 1.0f / 30, 0.2f}) {
        GameWorld world(cfg);
        world.setCombatMapRules(rules);
        world.bindGroundHeightResolver(rules.get(), [&](float, float z, float &y) {
            y = world.worldToGrid({0, 0, z}).y <= 1 ? 0.5f : 0.0f;
            return true;
        });
        auto &units = world.getPokemons();
        units.push_back(makeUnit(cfg, "jumper", PokemonSide::Player, 5, 1, 2.0f));
        units.push_back(makeUnit(cfg, "follower", PokemonSide::Player, 5, 0, 3.0f));
        units.push_back(makeUnit(cfg, "target", PokemonSide::Enemy, 5, 4, 0.0f));
        world.conformPokemonToGround();
        auto &jumper = units[0];
        jumper.animIdleIndex = 0;
        jumper.animJumpStartIndex = 1;
        jumper.animJumpLoopIndex = 2;
        jumper.animJumpLandIndex = 3;
        jumper.backendAnimDurationsSec = {1.0f, 0.25f, 0.20f, 0.30f};
        GameWorld reversed(cfg);
        reversed.setCombatMapRules(rules);
        reversed.bindGroundHeightResolver(rules.get(), [&](float, float z, float &y) {
            y = reversed.worldToGrid({0, 0, z}).y <= 1 ? 0.5f : 0.0f;
            return true;
        });
        reversed.getPokemons() = units;
        std::reverse(reversed.getPokemons().begin(), reversed.getPokemons().end());
        MovementSystem reverseMovement(&reversed, services, combat);
        MovementSystem movement(&world, services, combat);
        ScriptAPI api(&world, nullptr, services);
        bool start = false, airborne = false, landing = false, completed = false;
        for (int tick = 0; tick < 500; ++tick) {
            movement.update(ecs, dt);
            reverseMovement.update(ecs, dt);
            for (const auto &unit : units) {
                const auto *other = reversed.findUnitById(unit.id);
                if (!other || glm::distance(unit.position, other->position) > 0.00001f ||
                    unit.committedDest != other->committedDest || unit.ledgeJump.phase != other->ledgeJump.phase) {
                    outFail = "Ledge reservations or trajectories depend on unit storage order.";
                    return false;
                }
            }
            const auto phase = jumper.ledgeJump.phase;
            if (phase != LedgeJumpPhase::None) {
                api.faceEnemy(jumper.id, 0, 1);
                api.flush();
                if (!facesDirection(jumper, jumper.moveTo - jumper.moveFrom)) {
                    outFail = "Target-facing turned a jumping unit away from its drop during start, flight or landing.";
                    return false;
                }
                if (jumper.committedDest != glm::ivec2(5, 2) || api.canAttack(jumper.id)) {
                    outFail = "Jump lost its landing reservation or allowed an attack before recovery.";
                    return false;
                }
                const float y = jumper.position.y;
                const int clip = jumper.activeAnimIndex;
                const float clipTime = jumper.animTimeSec;
                GameWorld::DebugStateSnapshot snapshot;
                world.buildDebugStateSnapshot(snapshot);
                const auto saved = snapshot.boardUnits.front();
                const auto safePosition = phase == LedgeJumpPhase::Landing ? jumper.moveTo : jumper.moveFrom;
                if (glm::vec3(saved.posX, saved.posY, saved.posZ) != safePosition) {
                    outFail = "Editor reload saved an airborne position without a reservation.";
                    return false;
                }
                world.update(dt);
                world.conformPokemonToGround();
                if (jumper.position.y != y || jumper.activeAnimIndex != clip || jumper.animTimeSec != clipTime) {
                    outFail = "Ground conformance or locomotion overwrote jump trajectory/animation.";
                    return false;
                }
                if (phase == LedgeJumpPhase::Start) {
                    start = true;
                    if (jumper.position != jumper.moveFrom || clip != 1) {
                        outFail = "Jump start translated before launch.";
                        return false;
                    }
                } else if (phase == LedgeJumpPhase::Airborne) {
                    airborne = true;
                    if (clip != 2 || clipTime >= 0.2f) {
                        outFail = "Airborne clip did not loop independently.";
                        return false;
                    }
                    if (jumper.moveT < 0.5f && y < jumper.moveFrom.y) {
                        outFail = "Jump penetrated upper shelf.";
                        return false;
                    }
                } else {
                    landing = true;
                    if (jumper.position != jumper.moveTo || clip != 3) {
                        outFail = "Landing did not hold destination/clip.";
                        return false;
                    }
                }
                if (units[1].committedDest == glm::ivec2(5, 1) || units[1].committedDest == glm::ivec2(5, 2)) {
                    outFail = "Follower entered a reserved jump corridor.";
                    return false;
                }
            } else if (start) {
                completed = true;
                if (jumper.committedDest != glm::ivec2(-1) || jumper.position != jumper.moveTo || !api.canAttack(jumper.id)) {
                    outFail = "Landing did not release the reservation and attack lock.";
                    return false;
                }
                break;
            }
        }
        if (!start || !airborne || !landing || !completed) {
            outFail = "Jump sequence stalled or skipped a required phase.";
            return false;
        }
    }
    // Scripted movement cannot bypass the uphill wall, jump diagonally, steal a
    // landing, or replace a committed jump. Legal commits enter the same chain.
    {
        GameWorld world(cfg);
        world.setCombatMapRules(rules);
        auto &units = world.getPokemons();
        units.push_back(makeUnit(cfg, "scripted", PokemonSide::Player, 5, 2));
        ScriptAPI api(&world, nullptr, services);
        api.commitMove(units[0].id, 5, 1);
        api.flush();
        if (units[0].committedDest.x >= 0) {
            outFail = "Scripted commit climbed uphill.";
            return false;
        }
        units[0].position = gridToWorld(cfg, 5, 1);
        api.commitMove(units[0].id, 6, 2);
        api.flush();
        if (units[0].committedDest.x >= 0) {
            outFail = "Scripted commit bypassed cardinal ledge drop.";
            return false;
        }
        api.commitMove(units[0].id, 5, 2);
        api.flush();
        MovementSystem movement(&world, services, combat);
        movement.update(ecs, 0.01f);
        if (!units[0].ledgeJump.active()) {
            outFail = "Scripted drop skipped jump animations.";
            return false;
        }
        api.commitMove(units[0].id, 4, 1);
        api.flush();
        if (units[0].committedDest != glm::ivec2(5, 2)) {
            outFail = "Script replaced an active jump reservation.";
            return false;
        }
    }
    // Missing clips cannot deadlock, and a long fixed tick consumes all phases.
    PokemonInstance fallback;
    fallback.moveFrom = {0, 0.5f, 0};
    fallback.moveTo = {0, 0, 1};
    LedgeJump::begin(fallback, 1.0f);
    if (!LedgeJump::advance(fallback, 10.0f) || fallback.ledgeJump.active() || fallback.position != fallback.moveTo) {
        outFail = "Missing animation fallback failed to finish a drop.";
        return false;
    }
    const nlohmann::json modern = {{"clips", {{{"gltf_name", "jumpdown01_start"}, {"category", "status"}, {"duration_seconds", .4}}, {{"gltf_name", "jumpdown01_loop"}, {"category", "status"}}, {{"gltf_name", "land02"}, {"category", "misc"}}}}};
    const auto modernRoles = AnimSet::resolveLedgeJumpRoles(modern);
    const nlohmann::json legacy = {{"clips", {{{"gltf_name", "pm_landA01"}, {"category", "misc"}}, {{"gltf_name", "pm_landB01"}, {"category", "misc"}}, {{"gltf_name", "pm_landC01"}, {"category", "misc"}}}}};
    const auto legacyRoles = AnimSet::resolveLedgeJumpRoles(legacy);
    if (!modernRoles.start.valid || !modernRoles.loop.valid || !modernRoles.land.valid ||
        !legacyRoles.start.valid || !legacyRoles.loop.valid || !legacyRoles.land.valid) {
        outFail = "Modern and LGPE ledge animation families must both resolve.";
        return false;
    }
    return true;
}

bool test_movement_collision_regressions(std::string &outFail) {
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
    engine::CoreServices core;
    core.rng = &rng;
    core.time = &time;
    engine::ecs::World ecs(&core);
    const auto combat = ecs.create();
    ecs.add<game::CombatActive>(combat, game::CombatActive{true});
    const auto commit = [&](PokemonInstance &unit, int col, int row, float progress) {
        unit.moveFrom = unit.position;
        unit.moveTo = gridToWorld(cfg, col, row);
        unit.position = glm::mix(unit.moveFrom, unit.moveTo, progress);
        unit.committedDest = {col, row};
        unit.moveT = progress;
        unit.isMoving = true;
    };

    // A detour can point away from the enemy. Every travel direction must win
    // over combat/Lua facing, including a new queued commit and in-flight move.
    for (bool scripted : {false, true}) {
        for (bool airborne : {false, true}) {
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz) {
                    if (dx == 0 && dz == 0) continue;
                    GameWorld world(cfg);
                    auto &units = world.getPokemons();
                    units.push_back(makeUnit(cfg, "mover", PokemonSide::Player, 3, 3));
                    units.push_back(makeUnit(cfg, "target", PokemonSide::Enemy, 3 - 2 * dx, 3 - 2 * dz, 0));
                    auto &mover = units[0];
                    mover.usesAirLocomotion = airborne;
                    mover.airState = airborne ? AirLocomotionState::Airborne : AirLocomotionState::Grounded;
                    const glm::vec3 direction(static_cast<float>(dx), 0, static_cast<float>(dz));
                    ScriptAPI api(&world, nullptr, services);
                    if (scripted) {
                        api.faceTarget(mover.id, units[1].id);
                        api.commitMove(mover.id, 3 + dx, 3 + dz);
                        api.faceEnemy(mover.id, std::nullopt, std::nullopt);
                        api.flush();
                        if (!mover.isMoving || !facesDirection(mover, direction)) {
                            outFail = "Queued movement must face its step immediately, regardless of enemy-facing command order.";
                            return false;
                        }
                    } else {
                        commit(mover, 3 + dx, 3 + dz, .25f);
                    }
                    MovementSystem movement(&world, services, combat);
                    for (int tick = 0; tick < 3; ++tick) {
                        const auto before = mover.position;
                        movement.update(ecs, .03f);
                        if (glm::distance(before, mover.position) <= 1e-5f || !facesDirection(mover, mover.position - before)) {
                            outFail = "A traversing unit faced its enemy instead of its actual motion.";
                            return false;
                        }
                        // Combat runs after movement and can issue either command.
                        api.faceTarget(mover.id, units[1].id);
                        api.faceEnemy(mover.id, 3 - 2 * dx, 3 - 2 * dz);
                        api.flush();
                        if (!facesDirection(mover, direction)) {
                            outFail = "Target-facing overrode walking/flying direction.";
                            return false;
                        }
                    }
                    mover.isMoving = false;
                    mover.committedDest = {-1, -1};
                    api.faceTarget(mover.id, units[1].id);
                    api.flush();
                    if (!facesDirection(mover, units[1].position - mover.position)) {
                        outFail = "A stopped unit must resume facing its combat target.";
                        return false;
                    }
                    const float yaw = mover.rotation.y;
                    units[1].position = mover.position + glm::vec3(0, 1, 0);
                    api.faceTarget(mover.id, units[1].id);
                    api.flush();
                    if (!std::isfinite(mover.rotation.y) || std::abs(mover.rotation.y - yaw) > .001f) {
                        outFail = "Pure height changes must not reset horizontal facing.";
                        return false;
                    }
                }
        }
    }

    // Crowded Battle starts with opponents directly across from each other.
    // Equal grid-step counts must not pull the whole row toward the lowest ID.
    // Different speeds may change the nearest opponent, but should not send
    // an unobstructed opening approach backwards around the rest of the row.
    for (bool variedSpeeds : {false, true}) {
        for (float dt : {1.0f / 120.0f, 1.0f / 30.0f, .2f}) {
            GameWorld world(cfg);
            auto &units = world.getPokemons();
            const float speeds[] = {1.0f, 1.2f, .9f, 1.2f, 1.1f, 1.3f};
            for (int row : {6, 1})
                for (int col = 1; col <= 6; ++col)
                    units.push_back(makeUnit(cfg, "crowded_lane", row == 6 ? PokemonSide::Player : PokemonSide::Enemy,
                                             col, row, variedSpeeds ? speeds[col - 1] : 1.0f));
            MovementSystem movement(&world, services, combat);
            movement.update(ecs, 0.0f);
            for (const auto &unit : units) {
                const auto cell = world.worldToGrid(unit.position);
                if (unit.targetMemory.cell != game::arena::Cell{cell.x, cell.y == 6 ? 1 : 6} ||
                    unit.committedDest != glm::ivec2(cell.x, cell.y == 6 ? 5 : 2)) {
                    outFail = "Crowded native planning ignored the clear lane directly ahead in column " + std::to_string(cell.x);
                    return false;
                }
            }
            ScriptAPI api(&world, nullptr, services);
            const auto snapshots = api.listUnitsForMovement();
            for (std::size_t i = 0; i < units.size(); ++i) {
                const auto cell = world.worldToGrid(units[i].position);
                const int enemyRow = cell.y == 6 ? 1 : 6;
                if (snapshots[i].enemyCol != cell.x || snapshots[i].enemyRow != enemyRow ||
                    api.nearestEnemyCell(units[i].id) != std::pair<int, int>{cell.x, enemyRow}) {
                    outFail = "Crowded target query ignored the closer opponent directly ahead in column " + std::to_string(cell.x);
                    return false;
                }
            }
            for (int tick = 0; tick < static_cast<int>(5.0f / dt); ++tick) {
                std::vector<glm::vec3> before;
                for (const auto &unit : units)
                    before.push_back(unit.position);
                movement.update(ecs, dt);
                for (std::size_t i = 0; i < units.size(); ++i) {
                    const auto &unit = units[i];
                    const float forward = unit.side == PokemonSide::Player ? -1.0f : 1.0f;
                    if ((unit.position.z - before[i].z) * forward < -1e-5f ||
                        (!variedSpeeds && std::abs(unit.position.x - before[i].x) > 1e-5f)) {
                        outFail = "Crowded opening took an unnecessary detour: unit " + std::to_string(i) +
                                  " at tick " + std::to_string(tick) + " varied=" + std::to_string(variedSpeeds) +
                                  " destination=" + std::to_string(unit.committedDest.x) + "," + std::to_string(unit.committedDest.y);
                        return false;
                    }
                }
            }
            for (const auto &unit : units) {
                if (!api.isAdjacentToEnemy(unit.id)) {
                    outFail = "Crowded opening failed to reach melee within five seconds.";
                    return false;
                }
            }
        }
    }

    // Opponents approaching along an empty lane should meet in that lane.
    // In particular, a fresh reservation toward us is not a stationary obstacle
    // that requires a sidestep before the next update's rounded cell changes.
    for (int gap : {2, 5}) {
        for (bool horizontal : {false, true}) {
            for (float speed : {0.6f, 1.0f, 1.7f}) {
                for (float dt : {1.0f / 120.0f, 1.0f / 30.0f, 0.2f}) {
                    GameWorld world(cfg);
                    auto &units = world.getPokemons();
                    units.push_back(makeUnit(cfg, "approaching_a", PokemonSide::Player, horizontal ? 1 : 3, horizontal ? 3 : 1, 1.0f));
                    units.push_back(makeUnit(cfg, "approaching_b", PokemonSide::Enemy, horizontal ? 1 + gap : 3, horizontal ? 3 : 1 + gap, speed));
                    MovementSystem movement(&world, services, combat);
                    bool met = false;
                    for (int tick = 0; tick < 1200; ++tick) {
                        const float beforeA = horizontal ? units[0].position.x : units[0].position.z;
                        const float beforeB = horizontal ? units[1].position.x : units[1].position.z;
                        movement.update(ecs, dt);
                        if ((horizontal ? units[0].position.x : units[0].position.z) < beforeA - 0.00001f ||
                            (horizontal ? units[1].position.x : units[1].position.z) > beforeB + 0.00001f) {
                            outFail = "Head-on approach reversed before meeting.";
                            return false;
                        }
                        if (glm::distance(units[0].position, units[1].position) < cfg.cellSize - 0.001f) {
                            outFail = "Head-on meeting bypassed physical separation.";
                            return false;
                        }
                        for (const auto &unit : units) {
                            if (unit.committedDest.x >= 0 && (horizontal ? unit.committedDest.y : unit.committedDest.x) != 3) {
                                outFail = "Head-on approach left its empty lane: " + unit.name +
                                          " committed " + std::to_string(unit.committedDest.x) + "," + std::to_string(unit.committedDest.y) +
                                          " at tick " + std::to_string(tick) + " speed=" + std::to_string(speed) + " dt=" + std::to_string(dt);
                                return false;
                            }
                        }
                        if (units[0].committedDest.x < 0 && units[1].committedDest.x < 0 &&
                            world.combatMap().canEngageMelee(world.combatActor(units[0]), world.combatActor(units[1]))) {
                            met = true;
                            break;
                        }
                    }
                    if (!met) {
                        outFail = "Head-on opponents failed to settle into melee.";
                        return false;
                    }
                }
            }
        }
    }
    // A faster contender is planned first, but cannot steal a slower unit's
    // already committed destination before the slow unit reaches its midpoint.
    {
        GameWorld world(cfg);
        auto &units = world.getPokemons();
        units.push_back(makeUnit(cfg, "slow", PokemonSide::Player, 1, 1, .25f));
        units.push_back(makeUnit(cfg, "fast", PokemonSide::Player, 1, 3, 2.0f));
        units.push_back(makeUnit(cfg, "target", PokemonSide::Enemy, 5, 2, 0.0f));
        units.push_back(makeUnit(cfg, "east_wall", PokemonSide::Player, 3, 2, 0.0f));
        units.push_back(makeUnit(cfg, "southeast_wall", PokemonSide::Player, 3, 3, 0.0f));
        commit(units[0], 2, 2, .2f);
        MovementSystem movement(&world, services, combat);
        movement.update(ecs, .01f);
        if (units[0].committedDest != glm::ivec2(2, 2) ||
            units[1].committedDest == glm::ivec2(2, 2)) {
            outFail = "A faster unit stole an in-flight destination reservation.";
            return false;
        }
    }
    // The origin is still occupied by the trailing portion of a slow move,
    // even after rounding the moving position to the destination cell.
    {
        GameWorld world(cfg);
        auto &units = world.getPokemons();
        units.push_back(makeUnit(cfg, "leader", PokemonSide::Player, 2, 2, .25f));
        units.push_back(makeUnit(cfg, "follower", PokemonSide::Player, 1, 2, 4.0f));
        units.push_back(makeUnit(cfg, "target", PokemonSide::Enemy, 6, 2, 0.0f));
        commit(units[0], 3, 2, .6f);
        MovementSystem movement(&world, services, combat);
        movement.update(ecs, .01f);
        if (units[1].committedDest == glm::ivec2(2, 2)) {
            outFail = "A follower entered a slow mover's origin before it cleared the step.";
            return false;
        }
    }
    // Two occupied flank cells must not be treated as a diagonal shortcut.
    {
        GameWorld world(cfg);
        auto &units = world.getPokemons();
        units.push_back(makeUnit(cfg, "runner", PokemonSide::Player, 1, 1));
        units.push_back(makeUnit(cfg, "east_blocker", PokemonSide::Player, 2, 1, 0.0f));
        units.push_back(makeUnit(cfg, "south_blocker", PokemonSide::Player, 1, 2, 0.0f));
        units.push_back(makeUnit(cfg, "target", PokemonSide::Enemy, 6, 6, 0.0f));
        MovementSystem movement(&world, services, combat);
        movement.update(ecs, .01f);
        if (units[0].committedDest == glm::ivec2(2, 2) || units[0].committedDest.x < 0) {
            outFail = "The pathfinder must route around occupied corners rather than cut through them.";
            return false;
        }
    }
    // Reservations end on arrival, so queuing cannot permanently seal a lane.
    {
        GameWorld world(cfg);
        auto &units = world.getPokemons();
        units.push_back(makeUnit(cfg, "leader", PokemonSide::Player, 2, 2));
        units.push_back(makeUnit(cfg, "follower", PokemonSide::Player, 1, 2));
        units.push_back(makeUnit(cfg, "target", PokemonSide::Enemy, 6, 2, 0.0f));
        for (int col = 0; col < cfg.cols; ++col) {
            for (int row : {1, 3}) {
                units.push_back(makeUnit(cfg, "lane_wall", PokemonSide::Player, col, row, 0.0f));
            }
        }
        commit(units[0], 3, 2, .9f);
        MovementSystem movement(&world, services, combat);
        movement.update(ecs, .2f);
        movement.update(ecs, .01f);
        if (units[1].committedDest != glm::ivec2(2, 2)) {
            outFail = "The cleared origin did not become available to a waiting follower.";
            return false;
        }
    }
    // Interrupted movers retain their swept corridor while their presentation
    // blocks tiles. Removing the unit releases both endpoints on the next tick.
    for (bool capturing : {false, true}) {
        GameWorld world(cfg);
        auto &units = world.getPokemons();
        units.push_back(makeUnit(cfg, "departing", PokemonSide::Player, 2, 2));
        units.push_back(makeUnit(cfg, "waiting", PokemonSide::Player, 1, 2));
        units.push_back(makeUnit(cfg, "target", PokemonSide::Enemy, 6, 2, 0.0f));
        for (int col = 0; col < cfg.cols; ++col)
            for (int row : {1, 3})
                units.push_back(makeUnit(cfg, "wall", PokemonSide::Player, col, row, 0.0f));
        commit(units[0], 3, 2, .6f);
        units[0].alive = false;
        units[0].captureInProgress = capturing;
        units[0].fainting = !capturing;
        const bool priorFaintBlock = cfg.faintBlockTiles;
        cfg.faintBlockTiles = true;
        MovementSystem movement(&world, services, combat);
        movement.update(ecs, .01f);
        cfg.faintBlockTiles = priorFaintBlock;
        if (units[1].committedDest.x >= 0) {
            outFail = "Capture/faint presentation lost an interrupted mover's corridor.";
            return false;
        }
        units.erase(units.begin());
        movement.update(ecs, .01f);
        if (units[0].committedDest != glm::ivec2(2, 2)) {
            outFail = "Removing an interrupted mover left a stale reservation.";
            return false;
        }
    }
    // Planning and every script-facing query consume the same policy. The
    // injected restrictions exercise the boundary without enabling new rules.
    {
        struct Rules : game::arena::CombatMapRules {
            bool visible = false, melee = false;
            bool canTraverseCardinal(game::arena::Cell, game::arena::Cell, game::arena::TraversalCapabilities) const override { return false; }
            bool canPerceive(const game::arena::Actor &, const game::arena::Actor &) const override { return visible; }
            bool canEngageMelee(const game::arena::Actor &, const game::arena::Actor &) const override { return melee; }
        };
        GameWorld world(cfg);
        auto &units = world.getPokemons();
        units.push_back(makeUnit(cfg, "observer", PokemonSide::Player, 1, 1));
        units.push_back(makeUnit(cfg, "target", PokemonSide::Enemy, 4, 1, 0));
        auto rules = std::make_shared<Rules>();
        world.setCombatMapRules(rules);
        MovementSystem movement(&world, services, combat);
        ScriptAPI api(&world, nullptr, services);
        movement.update(ecs, 0);
        if (units[0].committedDest.x >= 0 || api.nearestEnemyCell(units[0].id).first >= 0 || api.listUnitsForMovement()[0].enemyCol >= 0) {
            outFail = "Movement or script targeting pursued a hidden opponent.";
            return false;
        }
        rules->visible = true;
        movement.update(ecs, 0);
        if (units[0].committedDest.x >= 0 || api.listUnitsForMovement()[0].enemyCol != 4) {
            outFail = "Visibility and traversal restrictions were conflated.";
            return false;
        }
        units[1].position = gridToWorld(cfg, 2, 1);
        if (api.isAdjacentToEnemy(units[0].id) || !api.enemiesAdjacent(units[0].id).empty() ||
            api.listUnitsForCombat()[0].adjacentEnemyCount || api.listUnitsForMovement()[0].adjacentToEnemy) {
            outFail = "Melee queries ignored the shared map policy.";
            return false;
        }
        rules->melee = true;
        if (!api.isAdjacentToEnemy(units[0].id) || api.enemiesAdjacent(units[0].id).size() != 1 ||
            api.listUnitsForCombat()[0].adjacentEnemyCount != 1 || !api.listUnitsForMovement()[0].adjacentToEnemy) {
            outFail = "Melee queries did not agree after a policy change.";
            return false;
        }
    }
    // Watch complete multi-unit approaches at different fixed steps and speeds.
    // Check swept separation, not just cell occupancy at the end of a frame.
    for (float dt : {1.0f / 120.0f, 1.0f / 30.0f, .2f}) {
        GameWorld world(cfg), reversed(cfg);
        auto &units = world.getPokemons();
        for (int row : {0, 1, 6, 7}) {
            for (int col : {0, 2, 4, 6}) {
                units.push_back(makeUnit(cfg, "crowd", row < 2 ? PokemonSide::Enemy : PokemonSide::Player,
                                         col, row, .6f + .3f * static_cast<float>((col + row) % 5)));
            }
        }
        reversed.getPokemons() = units;
        std::reverse(reversed.getPokemons().begin(), reversed.getPokemons().end());
        MovementSystem movement(&world, services, combat), reverseMovement(&reversed, services, combat);
        bool approached = false;
        for (int tick = 0; tick < static_cast<int>(12.0f / dt); ++tick) {
            std::vector<glm::vec2> before;
            for (const auto &unit : units)
                before.emplace_back(unit.position.x, unit.position.z);
            movement.update(ecs, dt);
            reverseMovement.update(ecs, dt);
            for (std::size_t i = 0; i < units.size(); ++i) {
                const auto &unit = units[i];
                const auto *otherOrder = reversed.findUnitById(unit.id);
                if (!otherOrder || glm::distance(unit.position, otherOrder->position) > 1e-5f ||
                    unit.committedDest != otherOrder->committedDest) {
                    outFail = "Movement conflicts depend on unit storage order.";
                    return false;
                }
                const glm::vec2 now(unit.position.x, unit.position.z);
                if (!facesDirection(unit, glm::vec3(now.x - before[i].x, 0, now.y - before[i].y))) {
                    outFail = "Crowded-path turns must face the actual direction of traversal.";
                    return false;
                }
                approached = approached || glm::distance(now, before[i]) > 1e-5f;
                for (std::size_t j = 0; j < i; ++j) {
                    const glm::vec2 relativeStart = before[i] - before[j];
                    const glm::vec2 relativeEnd = now - glm::vec2(units[j].position.x, units[j].position.z);
                    const glm::vec2 travel = relativeEnd - relativeStart;
                    const float lengthSq = glm::dot(travel, travel);
                    const float t = lengthSq > 1e-10f ? std::clamp(-glm::dot(relativeStart, travel) / lengthSq, 0.0f, 1.0f) : 0.0f;
                    if (glm::length(relativeStart + travel * t) < cfg.cellSize * .68f) {
                        outFail = "Units crossed or overlapped during a movement step at tick " + std::to_string(tick);
                        return false;
                    }
                    if (unit.committedDest.x >= 0 && unit.committedDest == units[j].committedDest) {
                        outFail = "Crowded movement assigned duplicate destinations.";
                        return false;
                    }
                }
            }
        }
        if (!approached) {
            outFail = "Collision prevention froze all units instead of resolving movement.";
            return false;
        }
    }
    return true;
}

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

    GameWorld hoveringWorld(cfg);
    hoveringWorld.setData(&db);
    hoveringWorld.setLogger(&log);
    auto& hoveringUnits = hoveringWorld.getPokemons();
    hoveringUnits.push_back(makeUnit(cfg, "butterfree", PokemonSide::Player, 1, 1));
    hoveringUnits.push_back(makeUnit(cfg, "hover_target", PokemonSide::Enemy, 6, 6));

    PokemonInstance& hoveringFlyer = hoveringUnits[0];
    hoveringFlyer.usesAirLocomotion = true;
    hoveringFlyer.airLiftY = 0.62f;
    hoveringFlyer.animGroundIdleIndex = 0;
    hoveringFlyer.animAirIdleIndex = 1;
    hoveringFlyer.animMoveIndex = 2;
    hoveringFlyer.backendAnimDurationsSec = {1.0f, 1.0f, 0.55f};

    MovementSystem hoveringMovement(&hoveringWorld, services, combatEntity);
    const glm::vec3 hoveringOrigin = hoveringFlyer.position;
    hoveringMovement.update(ecsWorld, 0.10f);
    if (!hoveringFlyer.isMoving ||
        glm::length(glm::vec2(hoveringFlyer.position.x - hoveringOrigin.x,
                              hoveringFlyer.position.z - hoveringOrigin.z)) <= 1e-5f) {
        outFail = "A continuously airborne flyer without an authored takeoff must not be held at its origin.";
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
