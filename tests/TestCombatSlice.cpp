// tests/TestCombatSlice.cpp
#include <string>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <sol/sol.hpp>

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
#include "game/config/GameDataDb.h"
#include "game/config/MovesConfigLoader.h"
#include "game/logging/LogBus.h"
#include "game/scripting/ScriptEventBus.h"
#include "game/scripting/LuaBindings_Internal.h"
#include "game/scripting/LuaBindings.h"
#include "game/systems/CombatSystem.h"

namespace {
PokemonInstance makeUnit(const GameConfigData& cfg,
                         const std::string& name,
                         PokemonSide side,
                         int col,
                         int row,
                         const std::string& fastMove) {
    PokemonInstance u;
    u.id = PokemonInstance::getNextUnitID();
    u.name = name;
    u.side = side;
    u.alive = true;
    u.position = gridToWorld(cfg, col, row);
    u.hp = 100;
    u.maxHP = 100;
    u.attack = 10;
    u.movementSpeed = 1.0f;
    u.fastMove = fastMove;
    u.chargedMove.clear();
    u.energy = 0;
    u.maxEnergy = 100;
    u.isMoving = false;
    u.moveT = 1.0f;
    u.committedDest = {-1, -1};
    return u;
}
} // namespace

bool test_combat_targeting_headless(std::string& outFail) {
    GameConfigData cfg;
    GameDataDb db;
    LogBus::Logger log;
    log.setEchoToStdout(false);
    log.setFeedEnabled(false);
    ScriptEventBus events;
    game::assets::DevAssetStore assets(engine::paths::dataRoot());
    engine::XorShift32 rng(99u);
    engine::ManualTimeSource time;
    if (!db.moves.loadConfig(engine::paths::data("config/moves_config.json"), &log) ||
        !db.attackAnims.loadConfig(engine::paths::data("config/attack_anim_config.json"), &log)) {
        outFail = "Failed to load combat targeting test data."; return false;
    }
    GameServices services(cfg, db, log, events, assets, rng, time);
    engine::CoreServices core;
    core.rng = &rng;
    core.time = &time;
    engine::ecs::World ecs(&core);
    const auto combatEntity = ecs.create();
    ecs.add<game::CombatActive>(combatEntity, game::CombatActive{true});
    const auto fighter = [&](PokemonSide side, int col, int row) {
        auto unit = makeUnit(cfg, "bulbasaur", side, col, row, "tackle");
        unit.animAttack1Index = 0;
        unit.attackDurationSec = 1.0f;
        unit.hp = unit.maxHP = 1000;
        return unit;
    };
    const auto finishAnimation = [](PokemonInstance& unit) {
        unit.attackTimerSec = 0;
        unit.currentAttackAnimIndex = unit.activeAnimIndex = -1;
        unit.pendingDamageActive = unit.pendingDamageApplied = false;
        unit.pendingDamageTargetId = -1;
    };
    struct Rules : game::arena::CombatMapRules {
        int hidden = -1, wall = -1;
        bool canTraverseCardinal(game::arena::Cell, game::arena::Cell, game::arena::TraversalCapabilities) const override { return true; }
        bool canPerceive(const game::arena::Actor&, const game::arena::Actor& b) const override { return b.id != hidden; }
        bool canEngageMelee(const game::arena::Actor&, const game::arena::Actor& b) const override { return b.id != wall; }
    };
    // Run the actual native and Lua combat drivers against the same scenarios.
    for (bool scripted : {false, true}) {
        const auto run = [&](GameWorld& world, const std::function<bool(const std::function<void()>&)>& scenario) {
            world.setData(&db);
            world.setLogger(&log);
            world.setRenderEnabled(false);
            ScriptAPI api(&world, nullptr, services);
            CombatSystem combat(&world, services, combatEntity);
            sol::state lua;
            if (scripted) {
                lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string, sol::lib::package, sol::lib::os);
                registerLuaBindings(lua, api);
                lua.script_file(engine::paths::data("scripts/systems/combat.lua"));
                lua["combat_init"]();
                api.flush();
            }
            const std::function<void()> tick = [&]() {
                if (scripted) {
                    sol::protected_function update = lua["combat_update"];
                    auto result = update(.25f);
                    if (!result.valid()) { sol::error error = result; throw std::runtime_error(error.what()); }
                    api.flush();
                }
                else combat.update(ecs, .25f);
            };
            return scenario(tick);
        };
        for (bool reverseOrder : {false, true}) {
            GameWorld world(cfg);
            auto& units = world.getPokemons();
            for (int row : {3, 4}) for (int col = 1; col <= 5; ++col) {
                auto unit = fighter(row == 4 ? PokemonSide::Player : PokemonSide::Enemy, col, row);
                unit.hp = 200 + col * 100; // Low-health diagonal neighbors must not steal focus.
                units.push_back(unit);
            }
            if (reverseOrder) std::reverse(units.begin(), units.end());
            if (!run(world, [&](const auto& tick) {
                // Collect each first swing without advancing its animation.
                for (int step = 0; step < 8 && std::any_of(units.begin(), units.end(), [](const auto& unit) { return !unit.pendingDamageActive; }); ++step) tick();
                for (const auto& unit : units) {
                    const auto* target = world.findUnitById(unit.pendingDamageTargetId);
                    if (!target || target->side == unit.side ||
                        world.worldToGrid(target->position).x != world.worldToGrid(unit.position).x) {
                        outFail = "Aligned 5v5 must attack head-on, regardless of neighboring HP or storage order. scripted=" + std::to_string(scripted) +
                                  " unit=" + std::to_string(unit.id) + " target=" + std::to_string(unit.pendingDamageTargetId) + " timer=" + std::to_string(unit.attackTimerSec);
                        return false;
                    }
                }
                return true;
            })) return false;
        }
        {
            GameWorld world(cfg);
            auto& units = world.getPokemons();
            units.push_back(fighter(PokemonSide::Player, 3, 3));
            units.push_back(fighter(PokemonSide::Enemy, 4, 4));
            units.push_back(fighter(PokemonSide::Enemy, 7, 7));
            if (!run(world, [&](const auto& tick) {
                units[0].isMoving = units[1].isMoving = units[2].isMoving = true;
                tick();
                if (units[0].pendingDamageActive) { outFail = "Moving unit began an attack. scripted=" + std::to_string(scripted); return false; }
                units[2].position = world.gridToWorld(3, 4);
                units[0].isMoving = false;
                tick();
                if (units[0].pendingDamageTargetId != units[2].id) {
                    outFail = "Passing a diagonal enemy during movement stole the first combat target."; return false;
                }
                return true;
            })) return false;
        }
        for (int invalidation = 0; invalidation < 7; ++invalidation) {
            GameWorld world(cfg);
            auto rules = std::make_shared<Rules>();
            world.setCombatMapRules(rules);
            auto& units = world.getPokemons();
            units.push_back(fighter(PokemonSide::Player, 3, 3));
            units.push_back(fighter(PokemonSide::Enemy, 4, 4));
            units.push_back(fighter(PokemonSide::Enemy, 7, 7));
            const int attackerId = units[0].id, originalId = units[1].id, fallbackId = units[2].id;
            if (!run(world, [&](const auto& tick) {
                units[1].isMoving = units[2].isMoving = true; // Prevent opponent attacks while testing the player's focus.
                auto& attacker = *world.findUnitById(attackerId);
                tick();
                if (attacker.pendingDamageTargetId != originalId) { outFail = "Failed to acquire initial diagonal target."; return false; }
                auto* fallback = world.findUnitById(fallbackId);
                fallback->position = world.gridToWorld(3, 4);
                fallback->hp = 10;
                tick();
                if (attacker.pendingDamageTargetId != originalId) { outFail = "Retargeted an attack already in progress."; return false; }
                const auto nextAttack = [&]() {
                    finishAnimation(attacker);
                    for (int step = 0; step < 80 && !attacker.pendingDamageActive; ++step) tick();
                    return attacker.pendingDamageTargetId;
                };
                if (nextAttack() != originalId) {
                    outFail = "A closer, weaker newcomer stole a still-attackable combat target. scripted=" + std::to_string(scripted); return false;
                }
                auto* original = world.findUnitById(originalId);
                switch (invalidation) {
                    case 0: original->alive = false; original->fainting = true; break;
                    case 1: original->captureInProgress = true; break;
                    case 2: original->position = world.gridToWorld(7, 6); break;
                    case 3: original->side = PokemonSide::Player; break;
                    case 4: rules->hidden = originalId; break;
                    case 5: rules->wall = originalId; break;
                    case 6: units.erase(std::remove_if(units.begin(), units.end(), [&](const auto& unit) { return unit.id == originalId; }), units.end()); break;
                }
                if (nextAttack() != fallbackId) {
                    outFail = "Invalid focus prevented retargeting: reason=" + std::to_string(invalidation) + " scripted=" + std::to_string(scripted); return false;
                }
                // Nothing attackable: finish the current cycle, then wait.
                world.findUnitById(fallbackId)->position = world.gridToWorld(0, 0);
                if (nextAttack() != -1) { outFail = "Started an attack without an attackable enemy."; return false; }
                return true;
            })) return false;
        }
    }
    return true;
}

bool test_combat_slice_headless(std::string& outFail) {
    GameConfigData cfg;
    GameDataDb db;
    LogBus::Logger log;
    log.setEchoToStdout(false);
    log.setFeedEnabled(false);

    ScriptEventBus events;
    game::assets::DevAssetStore assets(engine::paths::dataRoot());
    engine::XorShift32 rng(99u);
    engine::ManualTimeSource time;

    const std::string movesPath = engine::paths::data("config/moves_config.json");
    if (!db.moves.loadConfig(movesPath, nullptr)) {
        outFail = "Failed to load moves config: " + movesPath;
        return false;
    }

    GameServices services(cfg, db, log, events, assets, rng, time);
    GameWorld world(cfg);
    world.setData(&db);
    world.setLogger(&log);

    auto& units = world.getPokemons();
    units.push_back(makeUnit(cfg, "unit_a", PokemonSide::Player, 3, 3, "tackle"));
    units.push_back(makeUnit(cfg, "unit_b", PokemonSide::Enemy, 4, 3, "tackle"));

    const int hpA0 = units[0].hp;
    const int hpB0 = units[1].hp;

    engine::CoreServices core;
    core.rng = &services.rng;
    core.time = &services.time;

    engine::ecs::World ecsWorld(&core);
    engine::ecs::Entity combatEntity = ecsWorld.create();
    ecsWorld.add<game::CombatActive>(combatEntity, game::CombatActive{true});

    CombatSystem combat(&world, services, combatEntity);

    units[0].isMoving = true;
    units[0].moveT = 0.5f;
    units[1].isMoving = true;
    units[1].moveT = 0.5f;

    combat.update(ecsWorld, 0.1f);

    if (units[0].attackTimerSec > 0.0f || units[1].attackTimerSec > 0.0f) {
        outFail = "Units should not begin attack cycles while still flagged as moving.";
        return false;
    }
    if (units[0].hp != 100 || units[1].hp != 100) {
        outFail = "Combat should not apply damage while adjacent units are still in locomotion.";
        return false;
    }

    units[0].isMoving = false;
    units[0].moveT = 1.0f;
    units[1].isMoving = false;
    units[1].moveT = 1.0f;

    constexpr float dt = 0.25f;
    constexpr int steps = 80; // 20 seconds total
    for (int i = 0; i < steps; ++i) {
        combat.update(ecsWorld, dt);
    }

    const int hpA1 = units[0].hp;
    const int hpB1 = units[1].hp;

    if (hpA1 >= hpA0 && hpB1 >= hpB0) {
        outFail = "Combat slice produced no damage.";
        return false;
    }
    if (hpA1 < 0 || hpB1 < 0) {
        outFail = "Combat slice produced negative HP.";
        return false;
    }

    return true;
}
