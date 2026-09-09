#include "engine/core/Paths.h"
#include "engine/core/Random.h"
#include "engine/core/TimeSources.h"
#include "engine/core/Services.h"
#include "engine/core/ecs/World.h"
#include "game/GameWorld.h"
#include "game/GameConfig.h"
#include "game/GameServices.h"
#include "game/PhaseState.h"
#include "game/arena/AuthoredCombatMap.h"
#include "game/assets/DevAssetStore.h"
#include "game/config/GameDataDb.h"
#include "game/logging/LogBus.h"
#include "game/scripting/ScriptAPI.h"
#include "game/scripting/ScriptEventBus.h"
#include "game/systems/MovementSystem.h"
#include "game/systems/CombatSystem.h"
#include <cmath>
#include <stdexcept>

bool test_encounter_grass_memory(std::string &outFail) {
    try {
        const auto check = [](bool ok, const char *message) { if (!ok) throw std::runtime_error(message); };
        GameConfigData cfg;
        GameDataDb db;
        LogBus::Logger log;
        log.setEchoToStdout(false);
        ScriptEventBus events;
        game::assets::DevAssetStore assets(engine::paths::dataRoot());
        engine::XorShift32 rng(123);
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
                data.tiles[{x, z}] = {x, z, 0, 0, 0};
                data.playableCells.insert({x, z});
            }
        data.cover.push_back({"brush", {{{{500, 100}, {800, 100}, {800, 600}, {500, 600}}}}});
        const auto rules = std::make_shared<game::arena::AuthoredCombatMap>(data, game::arena::Cell{0, 0});
        {
            GameWorld world(cfg);
            world.setRenderEnabled(false);
            world.setCombatMapRules(rules);
            PokemonInstance observer, hidden;
            observer.id = PokemonInstance::getNextUnitID();
            observer.side = PokemonSide::Enemy;
            observer.position = world.gridToWorld(1, 7);
            observer.movementSpeed = 2;
            observer.targetMemory = {{5, 3}, 0, 0, 8};
            hidden.id = PokemonInstance::getNextUnitID();
            hidden.side = PokemonSide::Player;
            hidden.position = world.gridToWorld(5, 4);
            hidden.movementSpeed = 0;
            hidden.isMoving = true;
            hidden.moveFrom = hidden.position;
            hidden.moveTo = world.gridToWorld(5, 5);
            hidden.committedDest = {5, 5};
            MovementSystem movement(&world, services, combat);
            auto distant = hidden;
            distant.position = world.gridToWorld(7, 1);
            distant.isMoving = false;
            distant.committedDest = {-1, -1};
            for (int x = 0; x <= 2; ++x)
                for (int z = 0; z < 8; ++z) {
                    observer.position = world.gridToWorld(x, z);
                    world.getPokemons() = {observer, distant};
                    movement.update(ecs, 0.01f);
                    const auto expected = world.getPokemons()[0].committedDest;
                    world.getPokemons() = {observer, hidden};
                    movement.update(ecs, 0.01f);
                    check(world.getPokemons()[0].committedDest == expected,
                          "A hidden reservation steered a distant investigation.");
                }
            // At the occupied edge, retain the lead and wait for a safe step.
            observer.position = world.gridToWorld(4, 5);
            world.getPokemons() = {observer, hidden};
            movement.update(ecs, 0.01f);
            check(world.getPokemons()[0].committedDest.x < 0 && world.getPokemons()[0].targetMemory.active(),
                  "Investigation bypassed a hidden collision or discarded its lead.");
        }
        const auto run = [&](int hiddenRow, bool previouslySeen) {
            GameWorld world(cfg);
            world.setRenderEnabled(false);
            world.setCombatMapRules(rules);
            auto &units = world.getPokemons();
            for (int i = 0; i < 2; ++i) {
                PokemonInstance unit;
                unit.id = PokemonInstance::getNextUnitID();
                unit.side = i == 0 ? PokemonSide::Enemy : PokemonSide::Player;
                unit.position = world.gridToWorld(i == 0 ? 1 : 4, 3);
                unit.hp = unit.maxHP = 100;
                unit.movementSpeed = 2;
                units.push_back(unit);
            }
            MovementSystem movement(&world, services, combat);
            ScriptAPI api(&world, nullptr, services);
            if (previouslySeen) {
                // The observer sees this movement into the brush before the
                // target disappears. No hidden destination may update the lead.
                units[1].isMoving = true;
                units[1].committedDest = {5, 3};
                units[1].moveFrom = units[1].position;
                units[1].moveTo = world.gridToWorld(5, 3);
                movement.update(ecs, 0.01f);
                check(units[0].targetMemory.cell == game::arena::Cell{5, 3}, "Visible entry into brush was not remembered.");
            }
            units[1].position = world.gridToWorld(7, hiddenRow);
            units[1].isMoving = false;
            units[1].committedDest = {-1, -1};
            units[1].movementSpeed = 0;
            std::vector<glm::vec3> path;
            if (!previouslySeen) {
                movement.update(ecs, 0.05f);
                check(!units[0].targetMemory.active() && units[0].committedDest == glm::ivec2(1, 4),
                      "Never-seen enemy generated a memory instead of a patrol.");
                return path;
            }
            for (int tick = 0; tick < 20; ++tick) {
                movement.update(ecs, 0.05f);
                path.push_back(units[0].position);
                check(units[0].targetMemory.cell == game::arena::Cell{5, 3} && units[0].patrol.startColumn < 0,
                      "Hidden movement changed the remembered lead or replaced it with a patrol.");
                check(api.nearestEnemyCell(units[0].id).first < 0,
                      "Memory granted sight of the hidden enemy.");
                api.applyDamage(units[0].id, units[1].id, 10, std::nullopt, std::nullopt, std::nullopt);
                check(units[1].hp == 100 && units[0].coverRevealRemainingSec == 0,
                      "A remembered target could be attacked without current sight.");
            }
            bool found = false;
            for (int tick = 0; tick < 100; ++tick) {
                movement.update(ecs, 0.05f);
                const auto distance = units[0].position - units[1].position;
                check(glm::dot(distance, distance) >= 0.9f, "Investigating units overlapped.");
                if (api.nearestEnemyCell(units[0].id).first >= 0) {
                    found = true;
                    break;
                }
            }
            check(found && rules->coverGroup(world.combatActor(units[0])) >= 0,
                  "Investigator failed to enter brush and regain actual sight.");
            // Repositioning clears a previous combat lead.
            check(world.setEditorPreviewUnitTransform(units[0].id, world.gridToWorld(1, 3), {}, true),
                  "Memory reset fixture could not reposition the observer.");
            check(!units[0].targetMemory.active(), "Editor reposition retained stale enemy memory.");
            units[0].targetMemory = {{5, 3}, 0, 0, 0.01f};
            movement.update(ecs, 0.05f);
            check(!units[0].targetMemory.active() && units[0].patrol.startColumn == 1,
                  "Expired memory did not resume the ordinary patrol.");
            // A fresh visible enemy takes priority over a remembered patch.
            world.setEditorPreviewUnitTransform(units[0].id, world.gridToWorld(1, 3), {}, true);
            units[0].targetMemory = {{5, 3}, 0, 0, 8};
            auto visible = units[1];
            visible.id = PokemonInstance::getNextUnitID();
            visible.position = world.gridToWorld(1, 0);
            units.push_back(visible);
            movement.update(ecs, 0.05f);
            check(units[0].committedDest == glm::ivec2(1, 2), "Memory took priority over an actually visible enemy.");
            units[1].targetMemory = {{5, 3}, 0, 0, 8};
            world.healPlayerUnitsToFull();
            check(!units[1].targetMemory.active(), "Between-round recovery retained enemy memory.");
            return path;
        };
        run(1, false);
        check(run(1, true) == run(5, true), "Investigation followed hidden coordinates instead of the last observation.");
        return true;
    } catch (const std::exception &error) {
        outFail = error.what();
        return false;
    }
}

bool test_encounter_grass_gameplay(std::string &outFail) {
    try {
        const auto check = [](bool ok, const char *message) { if (!ok) throw std::runtime_error(message); };
        GameConfigData cfg;
        GameDataDb db;
        LogBus::Logger log;
        log.setEchoToStdout(false);
        ScriptEventBus events;
        game::assets::DevAssetStore assets(engine::paths::dataRoot());
        engine::XorShift32 rng(123);
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
                data.tiles[{x, z}] = {x, z, 0, 0, 0};
                data.playableCells.insert({x, z});
            }
        data.cover.push_back({"west", {{{{100, 100}, {300, 100}, {300, 300}, {100, 300}}}}});
        data.cover.push_back({"east", {{{{600, 500}, {800, 500}, {800, 800}, {600, 800}}}}});
        GameWorld world(cfg);
        world.setRenderEnabled(false);
        world.setCombatMapRules(std::make_shared<game::arena::AuthoredCombatMap>(data, game::arena::Cell{0, 0}));
        auto &units = world.getPokemons();
        const auto add = [&](PokemonSide side, int col, int row) {
            PokemonInstance u;
            u.id = PokemonInstance::getNextUnitID();
            u.side = side;
            u.alive = true;
            u.hp = u.maxHP = 100;
            u.position = world.gridToWorld(col, row);
            u.movementSpeed = 2;
            u.animIdleIndex = u.animMoveIndex = u.animAttack1Index = -1;
            units.push_back(u);
        };
        add(PokemonSide::Player, 1, 1);
        add(PokemonSide::Enemy, 7, 6);
        ScriptAPI api(&world, nullptr, services);
        // Run the real movement -> combat -> animation order. A concealed
        // pursuer must not make the searching opponent turn toward it after
        // movement has already selected a patrol step.
        const auto initialUnits = units;
        for (bool swapTeams : {false, true}) {
            units = initialUnits;
            units[0].position = world.gridToWorld(4, 6);
            if (swapTeams) std::swap(units[0].side, units[1].side);
            MovementSystem movement(&world, services, combat);
            CombatSystem fighting(&world, services, combat);
            for (int tick = 0; tick < 24; ++tick) {
                check(!world.combatMap().canPerceive(world.combatActor(units[0]), world.combatActor(units[1])),
                      "Pursuit fixture left concealment too early.");
                movement.update(ecs, 1.0f / 60.0f);
                check(units[0].patrol.startColumn == 4 && units[0].committedDest.x == 4 &&
                          units[0].committedDest.y == (swapTeams ? 7 : 5),
                      "Outside unit chased a concealed pursuer instead of patrolling.");
                check(units[1].isMoving && units[1].patrol.startColumn < 0,
                      "Concealed unit did not pursue its visible opponent.");
                const float patrolFacing = units[0].rotation.y;
                fighting.update(ecs, 1.0f / 60.0f);
                check(std::abs(units[0].rotation.y - patrolFacing) < 0.001f,
                      "Combat facing leaked the concealed pursuer's position to the searching unit.");
                world.update(1.0f / 60.0f);
                check(api.nearestEnemyCell(units[0].id).first < 0 && units[1].coverRevealRemainingSec == 0,
                      "Moving in grass revealed the pursuer.");
            }
        }
        units = initialUnits;
        units[0].position = world.gridToWorld(4, 6);
        units[0].movementSpeed = 0;
        units[0].rotation.y = 180;
        {
            MovementSystem movement(&world, services, combat);
            CombatSystem fighting(&world, services, combat);
            movement.update(ecs, 1.0f / 60.0f);
            fighting.update(ecs, 1.0f / 60.0f);
            check(units[0].rotation.y == 180, "Idle combat facing tracked a moving concealed enemy.");
        }
        units = initialUnits;
        // Explicit queued facing must also recheck sight at execution time.
        units[0].position = world.gridToWorld(4, 6);
        units[1].position = world.gridToWorld(7, 4);
        api.faceTarget(units[0].id, units[1].id);
        units[1].position = world.gridToWorld(7, 6);
        units[0].rotation.y = 180;
        api.flush();
        check(units[0].rotation.y == 180, "Queued target facing tracked an enemy after it entered cover.");
        units[1].coverRevealRemainingSec = 1;
        api.faceTarget(units[0].id, units[1].id);
        api.flush();
        check(std::abs(units[0].rotation.y - 90.0f) < 0.001f, "Revealed enemy could not be faced.");
        units = initialUnits;
        // Replay the Grass Test's real authored patches. Rattata remains in
        // dense grass after its first step from (7,4) to (7,5); Bulbasaur must
        // keep searching even though Rattata can pursue it from that border.
        {
            std::string mapText, error;
            game::arena::ArenaMapData authored;
            check(assets.readText("config/environment/route1_pilot_gameplay.json", mapText, &error) && authored.load(mapText, &error),
                  "Grass Test map fixture failed to load.");
            world.setCombatMapRules(std::make_shared<game::arena::AuthoredCombatMap>(std::move(authored), game::arena::Cell{17, -10}));
            units[0].position = world.gridToWorld(2, 6);
            units[1].position = world.gridToWorld(7, 4);
            MovementSystem movement(&world, services, combat);
            CombatSystem fighting(&world, services, combat);
            bool crossedOldBoundary = false;
            for (int tick = 0; tick < 60; ++tick) {
                movement.update(ecs, 1.0f / 60.0f);
                fighting.update(ecs, 1.0f / 60.0f);
                world.update(1.0f / 60.0f);
                crossedOldBoundary |= units[1].position.z > world.gridToWorld(7, 4).z + cfg.cellSize * 0.5f;
                check(api.nearestEnemyCell(units[0].id).first < 0 && units[0].patrol.startColumn == 2,
                      "Bulbasaur acquired Rattata before it left the Grass Test's dense border.");
            }
            check(crossedOldBoundary, "Grass Test replay never reached the formerly exposed border.");
            world.setCombatMapRules(std::make_shared<game::arena::AuthoredCombatMap>(data, game::arena::Cell{0, 0}));
            units = initialUnits;
        }
        check(!world.isVisibleToPlayer(units[1]), "Enemy in separate patch was visible.");
        world.setShowConcealedUnits(true);
        check(world.isVisibleToPlayer(units[1]) && api.nearestEnemyCell(units[0].id).first < 0,
              "Debug view changed gameplay targeting.");
        world.setShowConcealedUnits(false);
        check(api.listUnitsForMovement()[0].enemyCol < 0, "Movement snapshot leaked a hidden target.");
        check(world.getNearestEnemyPosition(units[0]) == units[0].position, "Facing query leaked hidden enemy.");
        const int hp = units[1].hp;
        api.applyDamage(units[0].id, units[1].id, 10, std::nullopt, std::nullopt, std::nullopt);
        check(units[1].hp == hp && units[0].coverRevealRemainingSec == 0, "Rejected attack dealt damage or revealed attacker.");
        // A teammate in the patch grants player presentation sight, not remote
        // targeting knowledge to an individual Pokemon outside that patch.
        add(PokemonSide::Player, 6, 6);
        check(world.isVisibleToPlayer(units[1]) && api.nearestEnemyCell(units[0].id).first < 0,
              "Team presentation and individual targeting were conflated.");
        units[2].alive = false;
        check(!world.isVisibleToPlayer(units[1]), "Fainted observer retained sight.");
        units.pop_back();
        // A legal attack from concealment opens a timed retaliation window.
        units[0].position = world.gridToWorld(5, 6);
        api.applyDamage(units[1].id, units[0].id, 5, std::nullopt, std::nullopt, std::nullopt);
        check(units[0].hp < 100 && world.isVisibleToPlayer(units[1]), "Attacking from grass did not reveal the attacker.");
        world.update(0.5f);
        check(world.isVisibleToPlayer(units[1]), "Reveal ended too soon.");
        world.update(1.0f);
        check(!world.isVisibleToPlayer(units[1]), "Expired reveal left enemy visible.");
        // New targeting rechecks concealment; a scheduled hit still lands.
        units[1].position = world.gridToWorld(4, 6);
        check(db.attackAnims.loadConfig(engine::paths::data("config/attack_anim_config.json"), &log), "Attack timing fixture failed to load.");
        world.setData(&db);
        units[0].name = "bulbasaur";
        units[0].animFps = 60;
        units[0].attackDurationSec = 1;
        units[0].animAttack1Index = 0;
        api.applyDamage(units[0].id, units[1].id, 5, 1.0f, std::string("tackle"), std::string("fast"));
        check(units[0].attackTimerSec > 0 && units[0].pendingDamageActive, "Animated attack fixture did not schedule a hit.");
        units[1].position = world.gridToWorld(7, 6);
        const int before = units[1].hp;
        world.update(1.01f);
        check(units[1].hp < before, "Entering grass cancelled an already scheduled impact.");
        check(units[0].coverRevealRemainingSec > 1.0f, "Long attack did not retain the post-attack reveal.");
        // Both teams concealed: actual movement must search, respect occupied
        // corridors and eventually acquire a target without hidden coordinates.
        units[0].position = world.gridToWorld(1, 1);
        units[0].attackTimerSec = 0;
        for (auto &u : units) {
            u.coverRevealRemainingSec = 0;
            u.isMoving = false;
            u.committedDest = {-1, -1};
            u.patrol = {};
        }
        MovementSystem movement(&world, services, combat);
        bool encountered = false, searched = false;
        for (int tick = 0; tick < 1200; ++tick) {
            movement.update(ecs, 1.0f / 60.0f);
            searched |= units[0].patrol.startColumn >= 0 && units[0].isMoving;
            const auto d = units[0].position - units[1].position;
            check(d.x * d.x + d.z * d.z > 0.2f, "Searching units overlapped.");
            if (world.combatMap().canEngageMelee(world.combatActor(units[0]), world.combatActor(units[1]))) {
                encountered = true;
                break;
            }
        }
        check(searched && encountered, "Separate concealed teams failed to search and re-engage.");
        return true;
    } catch (const std::exception &error) {
        outFail = error.what();
        return false;
    }
}
