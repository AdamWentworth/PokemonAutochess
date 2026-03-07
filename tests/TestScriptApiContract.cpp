// tests/TestScriptApiContract.cpp
#include <cmath>
#include <string>
#include <unordered_map>

#include <sol/sol.hpp>

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
#include "game/scripting/LuaBindings.h"
#include "game/scripting/ScriptAPI.h"
#include "game/scripting/ScriptEventBus.h"
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

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (!condition) {
        outFail = message;
        return false;
    }
    return true;
}
} // namespace

bool test_script_api_contract(std::string& outFail) {
    GameConfigData cfg;
    GameDataDb db;
    LogBus::Logger log;
    log.setEchoToStdout(false);
    log.setFeedEnabled(false);

    ScriptEventBus events;
    game::assets::DevAssetStore assets(engine::paths::dataRoot());
    engine::XorShift32 rng(7u);
    engine::ManualTimeSource time;

    const std::string movesPath = engine::paths::data("config/moves_config.json");
    if (!db.moves.loadConfig(movesPath, nullptr)) {
        outFail = "Failed to load moves config: " + movesPath;
        return false;
    }
    const std::string attackAnimPath = engine::paths::data("config/attack_anim_config.json");
    if (!db.attackAnims.loadConfig(attackAnimPath, &log)) {
        outFail = "Failed to load attack anim config: " + attackAnimPath;
        return false;
    }
    const std::string pokemonPath = engine::paths::data("config/pokemon_config.json");
    if (!db.pokemon.loadConfig(pokemonPath, nullptr)) {
        outFail = "Failed to load pokemon config: " + pokemonPath;
        return false;
    }

    GameServices services(cfg, db, log, events, assets, rng, time);
    bool quitRequested = false;
    services.requestQuit = [&quitRequested]() { quitRequested = true; };
    GameWorld world(cfg);
    world.setData(&db);
    world.setLogger(&log);

    auto& units = world.getPokemons();
    PokemonInstance a = makeUnit(cfg, "unit_a", PokemonSide::Player, 1, 1);
    a.fastMove = "tackle";
    a.chargedMove = "seed_bomb";
    units.push_back(a);

    PokemonInstance b = makeUnit(cfg, "unit_b", PokemonSide::Enemy, 2, 1);
    units.push_back(b);

    PokemonInstance c = makeUnit(cfg, "unit_c", PokemonSide::Enemy, 6, 6);
    units.push_back(c);

    ScriptAPI api(&world, nullptr, services);
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
    registerLuaBindings(lua, api);

    // Direct API world-state surface.
    if (!expect(api.getMoney() == cfg.startingCash, "Initial ScriptAPI money mismatch.", outFail)) return false;
    api.addMoney(17);
    if (!expect(api.getMoney() == cfg.startingCash + 17, "addMoney/getMoney mismatch.", outFail)) return false;
    if (!expect(api.spendMoney(7), "spendMoney should succeed with available funds.", outFail)) return false;
    if (!expect(api.getMoney() == cfg.startingCash + 10, "spendMoney did not reduce funds as expected.", outFail)) return false;
    if (!expect(!api.spendMoney(cfg.startingCash + 1000), "spendMoney should fail when amount exceeds funds.", outFail)) return false;

    api.addItem("potion", 2);
    if (!expect(api.getItemCount("potion") == 2, "addItem/getItemCount mismatch.", outFail)) return false;
    if (!expect(api.consumeItem("potion", 1), "consumeItem should succeed for available quantity.", outFail)) return false;
    if (!expect(api.getItemCount("potion") == 1, "consumeItem did not reduce quantity.", outFail)) return false;

    if (!expect(api.getPokemonCatchRate("bulbasaur") > 0.0f, "getPokemonCatchRate should resolve configured species.", outFail)) return false;
    if (!expect(api.getPokemonCatchRate("missing_species") == 0.0f, "getPokemonCatchRate should return 0 for missing species.", outFail)) return false;

    // Combat readiness helpers should reflect active/combat-eligible state.
    const int idAReady = units[0].id;
    units[0].usesAirLocomotion = false;
    units[0].captureInProgress = false;
    units[0].attackTimerSec = 0.0f;
    if (!expect(api.canAttack(idAReady), "canAttack should be true for active grounded unit.", outFail)) return false;
    if (!expect(api.attackReady(idAReady), "attackReady should be true when attack timer is zero.", outFail)) return false;

    units[0].attackTimerSec = 0.2f;
    if (!expect(api.canAttack(idAReady), "canAttack should stay true while cooldown is active.", outFail)) return false;
    if (!expect(!api.attackReady(idAReady), "attackReady should be false while attack timer is positive.", outFail)) return false;

    units[0].captureInProgress = true;
    if (!expect(!api.canAttack(idAReady), "canAttack should be false for units in capture state.", outFail)) return false;
    if (!expect(!api.attackReady(idAReady), "attackReady should be false for units in capture state.", outFail)) return false;
    units[0].captureInProgress = false;
    units[0].attackTimerSec = 0.0f;

    // attackMinRequestSec should resolve configured move cadence by species + move kind.
    GameWorld cadenceWorld(cfg);
    cadenceWorld.setData(&db);
    cadenceWorld.setLogger(&log);
    auto& cadenceUnits = cadenceWorld.getPokemons();
    PokemonInstance cadenceUnit = makeUnit(cfg, "bulbasaur", PokemonSide::Player, 0, 0);
    cadenceUnits.push_back(cadenceUnit);
    ScriptAPI cadenceApi(&cadenceWorld, nullptr, services);
    const float minReq = cadenceApi.attackMinRequestSec(cadenceUnits[0].id,
                                                        std::optional<std::string>("vine_whip"),
                                                        std::nullopt);
    if (!expect(minReq >= 1.19f && minReq <= 1.21f,
                "attackMinRequestSec should match configured min request cadence for bulbasaur vine_whip.", outFail)) return false;
    if (!expect(cadenceApi.attackMinRequestSec(-999, std::nullopt, std::nullopt) == 0.0f,
                "attackMinRequestSec should return 0 for unknown unit id.", outFail)) return false;

    // Combat-balance multipliers should match side-aware products and clamp negatives.
    const int idPlayer = units[0].id;
    const int idEnemy = units[1].id;
    {
        GameWorld::CombatBalance balance;
        balance.playerDamageMult = 1.5f;
        balance.enemyDamageTakenMult = 0.5f;
        balance.enemyDamageMult = 2.0f;
        balance.playerDamageTakenMult = 0.25f;
        world.setCombatBalance(balance);
    }
    if (!expect(std::fabs(api.getDamageMultiplier(idPlayer, idEnemy) - 0.75f) < 0.0001f,
                "getDamageMultiplier mismatch for player -> enemy path.", outFail)) return false;
    if (!expect(std::fabs(api.getDamageMultiplier(idEnemy, idPlayer) - 0.5f) < 0.0001f,
                "getDamageMultiplier mismatch for enemy -> player path.", outFail)) return false;

    {
        GameWorld::CombatBalance balance;
        balance.playerDamageMult = -2.0f;
        balance.enemyDamageTakenMult = 3.0f;
        world.setCombatBalance(balance);
    }
    if (!expect(std::fabs(api.getDamageMultiplier(idPlayer, idEnemy) - 0.0f) < 0.0001f,
                "getDamageMultiplier should clamp negative multipliers to zero.", outFail)) return false;

    world.resetCombatBalance();
    if (!expect(std::fabs(api.getDamageMultiplier(-999, idEnemy) - 1.0f) < 0.0001f,
                "getDamageMultiplier should default to 1 for unknown units.", outFail)) return false;

    api.setGameMode("adventure");
    if (!expect(api.getGameMode() == "adventure", "setGameMode/getGameMode mismatch for valid mode.", outFail)) return false;
    api.setGameMode("invalid_mode");
    if (!expect(api.getGameMode() == "adventure", "setGameMode should ignore invalid mode values.", outFail)) return false;

    api.setHasStartedGame(true);
    if (!expect(api.getHasStartedGame(), "setHasStartedGame/getHasStartedGame mismatch.", outFail)) return false;
    api.setHasStartedGame(false);

    int videoW = -1;
    int videoH = -1;
    bool videoFullscreen = false;
    services.applyVideoMode = [&](int w, int h, bool fs) {
        videoW = w;
        videoH = h;
        videoFullscreen = fs;
        return true;
    };
    if (!expect(api.setVideoMode(320, 200, true), "setVideoMode should invoke applyVideoMode callback.", outFail)) return false;
    if (!expect(videoW == 640 && videoH == 360 && videoFullscreen,
                "setVideoMode should clamp to minimum resolution before callback.", outFail)) return false;
    services.queryVideoMode = []() {
        GameServices::VideoMode vm;
        vm.width = 1920;
        vm.height = 1080;
        vm.fullscreen = true;
        return vm;
    };
    const GameServices::VideoMode vm = api.getVideoMode();
    if (!expect(vm.width == 1920 && vm.height == 1080 && vm.fullscreen,
                "getVideoMode should return queryVideoMode callback value.", outFail)) return false;

    api.setClassicShopCards({
        {"pikachu", 0, -2},
        {"", 2, 3},
    });
    const auto shopCards = api.getClassicShopCards();
    if (!expect(shopCards.size() == 1, "setClassicShopCards should filter empty names.", outFail)) return false;
    if (!expect(shopCards[0].name == "pikachu" && shopCards[0].level == 1 && shopCards[0].cost == 0,
                "setClassicShopCards should preserve world-side sanitization.", outFail)) return false;
    api.clearClassicShopCards();
    if (!expect(api.getClassicShopCards().empty(), "clearClassicShopCards should clear world shop cards.", outFail)) return false;

    // Command queue energy semantics.
    const int idAEnergy = units[0].id;
    units[0].energy = 10;
    const int projectedEnergy = api.addEnergy(idAEnergy, 15);
    if (!expect(projectedEnergy == 25, "addEnergy projected value mismatch before flush.", outFail)) return false;
    if (!expect(units[0].energy == 10, "addEnergy should queue without immediate mutation.", outFail)) return false;
    api.flush();
    if (!expect(units[0].energy == 25, "addEnergy should apply queued delta on flush.", outFail)) return false;

    if (!expect(api.setEnergy(idAEnergy, 999), "setEnergy should queue for valid unit id.", outFail)) return false;
    api.flush();
    if (!expect(units[0].energy == units[0].maxEnergy, "setEnergy should clamp to maxEnergy on apply.", outFail)) return false;

    if (!expect(api.setEnergy(idAEnergy, -5), "setEnergy should accept negative values and clamp on apply.", outFail)) return false;
    api.flush();
    if (!expect(units[0].energy == 0, "setEnergy should clamp negative values to zero on apply.", outFail)) return false;

    if (!expect(!api.setEnergy(-123, 5), "setEnergy should fail for unknown unit id.", outFail)) return false;
    if (!expect(api.addEnergy(-123, 5) == 0, "addEnergy should return 0 for unknown unit id.", outFail)) return false;

    // world_list_units should return 3 entries with core fields.
    sol::function listFn = lua["world_list_units"];
    if (!expect(listFn.valid(), "world_list_units binding missing.", outFail)) return false;
    sol::table list = listFn();
    std::unordered_map<int, sol::table> byId;
    for (auto& kv : list) {
        sol::table t = kv.second.as<sol::table>();
        const int id = t["id"];
        byId[id] = t;
    }
    if (!expect(byId.size() == 3, "world_list_units returned unexpected size.", outFail)) return false;

    const int idA = idPlayer;
    if (!expect(byId.count(idA) == 1, "world_list_units missing unit_a.", outFail)) return false;
    sol::table ta = byId[idA];
    const std::string name = ta["name"].get_or(std::string());
    const bool alive = ta["alive"].get_or(false);
    const int col = ta["col"].get_or(-1);
    const int row = ta["row"].get_or(-1);
    if (!expect(name == "unit_a", "world_list_units name mismatch.", outFail)) return false;
    if (!expect(alive == true, "world_list_units alive mismatch.", outFail)) return false;
    if (!expect(col == 1 && row == 1, "world_list_units grid mismatch.", outFail)) return false;
    if (!expect(!api.getUnitSnapshot(-999).has_value(), "getUnitSnapshot should return nullopt for unknown unit.", outFail)) return false;

    sol::function snapFn = lua["world_get_unit_snapshot"];
    if (!expect(snapFn.valid(), "world_get_unit_snapshot binding missing.", outFail)) return false;
    sol::table snapA = snapFn(idA);
    if (!expect(snapA.valid(), "world_get_unit_snapshot returned invalid table.", outFail)) return false;
    if (!expect(snapA["name"].get_or(std::string()) == name, "world_get_unit_snapshot name mismatch.", outFail)) return false;
    if (!expect(snapA["alive"].get_or(false) == alive, "world_get_unit_snapshot alive mismatch.", outFail)) return false;
    if (!expect(snapA["col"].get_or(-1) == col && snapA["row"].get_or(-1) == row,
                "world_get_unit_snapshot grid mismatch.", outFail)) return false;

    // Move accessors should return configured moves.
    sol::function fastFn = lua["unit_fast_move"];
    sol::function chargedFn = lua["unit_charged_move"];
    if (!expect(fastFn.valid(), "unit_fast_move binding missing.", outFail)) return false;
    if (!expect(chargedFn.valid(), "unit_charged_move binding missing.", outFail)) return false;
    const std::string fastMove = fastFn(idA);
    const std::string chargedMove = chargedFn(idA);
    if (!expect(fastMove == "tackle", "unit_fast_move returned unexpected value.", outFail)) return false;
    if (!expect(chargedMove == "seed_bomb", "unit_charged_move returned unexpected value.", outFail)) return false;

    // move_get should expose move metadata.
    sol::function moveGet = lua["move_get"];
    if (!expect(moveGet.valid(), "move_get binding missing.", outFail)) return false;
    sol::table moveT = moveGet("tackle");
    if (!expect(moveT.valid(), "move_get returned invalid table.", outFail)) return false;
    const std::string kind = moveT["kind"].get_or(std::string());
    if (!expect(kind == "fast", "move_get kind mismatch for tackle.", outFail)) return false;

    // Nearest enemy cell + adjacency.
    sol::function nearestFn = lua["world_nearest_enemy_cell"];
    if (!expect(nearestFn.valid(), "world_nearest_enemy_cell binding missing.", outFail)) return false;
    std::pair<int, int> nearest = nearestFn(idA);
    if (!expect(nearest.first == 2 && nearest.second == 1, "world_nearest_enemy_cell mismatch.", outFail)) return false;

    sol::function adjacentFn = lua["world_is_adjacent_to_enemy"];
    if (!expect(adjacentFn.valid(), "world_is_adjacent_to_enemy binding missing.", outFail)) return false;
    const bool adjacent = adjacentFn(idA);
    if (!expect(adjacent == true, "world_is_adjacent_to_enemy expected true.", outFail)) return false;

    sol::function enemiesAdjacentFn = lua["world_enemies_adjacent"];
    if (!expect(enemiesAdjacentFn.valid(), "world_enemies_adjacent binding missing.", outFail)) return false;
    sol::table adjacentIds = enemiesAdjacentFn(idA);
    if (!expect(adjacentIds.size() == 1 && adjacentIds[1].get_or(-1) == idEnemy,
                "world_enemies_adjacent should return only the one adjacent enemy.", outFail)) return false;
    units[1].captureInProgress = true;
    adjacentIds = enemiesAdjacentFn(idA);
    if (!expect(adjacentIds.size() == 0,
                "world_enemies_adjacent should filter enemies in capture state.", outFail)) return false;
    units[1].captureInProgress = false;

    // Commit move queues and applies after flush.
    sol::function commitFn = lua["world_commit_move"];
    if (!expect(commitFn.valid(), "world_commit_move binding missing.", outFail)) return false;
    commitFn(idA, 1, 2);
    api.flush();
    if (!expect(units[0].isMoving, "commit_move did not mark unit as moving.", outFail)) return false;
    if (!expect(units[0].committedDest.x == 1 && units[0].committedDest.y == 2,
                "commit_move did not set committedDest.", outFail)) return false;

    // Apply move should snap to target and clear moving state.
    sol::function applyFn = lua["world_apply_move"];
    if (!expect(applyFn.valid(), "world_apply_move binding missing.", outFail)) return false;
    applyFn(idA, 1, 3);
    api.flush();
    const auto cell = worldToGrid(cfg, units[0].position);
    if (!expect(cell.x == 1 && cell.y == 3, "apply_move did not update position.", outFail)) return false;
    if (!expect(!units[0].isMoving, "apply_move did not clear moving state.", outFail)) return false;

    // Face enemy should change rotation.
    const float prevRot = units[0].rotation.y;
    sol::function faceFn = lua["world_face_enemy"];
    sol::function faceTargetFn = lua["world_face_target"];
    if (!expect(faceFn.valid(), "world_face_enemy binding missing.", outFail)) return false;
    if (!expect(faceTargetFn.valid(), "world_face_target binding missing.", outFail)) return false;
    faceFn(idA);
    api.flush();
    if (!expect(std::fabs(units[0].rotation.y - prevRot) > 0.01f, "world_face_enemy did not update rotation.", outFail)) return false;

    // Facing helpers should not produce NaN when target direction has zero length.
    const int idBForFacing = units[1].id;
    units[1].position = units[0].position;
    const float rotBeforeFaceTarget = units[0].rotation.y;
    faceTargetFn(idA, idBForFacing);
    api.flush();
    if (!expect(std::isfinite(units[0].rotation.y), "world_face_target should keep rotation finite on zero-length direction.", outFail)) return false;
    if (!expect(std::fabs(units[0].rotation.y - rotBeforeFaceTarget) < 0.0001f,
                "world_face_target should leave rotation unchanged on zero-length direction.", outFail)) return false;

    const auto selfCell = worldToGrid(cfg, units[0].position);
    const float rotBeforeFaceEnemyTarget = units[0].rotation.y;
    faceFn(idA, selfCell.x, selfCell.y);
    api.flush();
    if (!expect(std::isfinite(units[0].rotation.y), "world_face_enemy should keep rotation finite for explicit self target.", outFail)) return false;
    if (!expect(std::fabs(units[0].rotation.y - rotBeforeFaceEnemyTarget) < 0.0001f,
                "world_face_enemy should leave rotation unchanged for explicit self target.", outFail)) return false;

    const bool oldCaptureB = units[1].captureInProgress;
    const bool oldCaptureC = units[2].captureInProgress;
    units[1].captureInProgress = true;
    units[2].captureInProgress = true;
    const float rotBeforeNoEnemy = units[0].rotation.y;
    faceFn(idA);
    api.flush();
    if (!expect(std::isfinite(units[0].rotation.y), "world_face_enemy should keep rotation finite when no active enemies exist.", outFail)) return false;
    if (!expect(std::fabs(units[0].rotation.y - rotBeforeNoEnemy) < 0.0001f,
                "world_face_enemy should leave rotation unchanged when no active enemies exist.", outFail)) return false;
    units[1].captureInProgress = oldCaptureB;
    units[2].captureInProgress = oldCaptureC;

    // Emit + drain events should return a payload.
    sol::function emitFn = lua["emit"];
    sol::function emitGoldFn = lua["emit_gold"];
    if (!expect(emitFn.valid(), "emit binding missing.", outFail)) return false;
    if (!expect(emitGoldFn.valid(), "emit_gold binding missing.", outFail)) return false;
    emitFn("test_event", "payload");
    emitGoldFn("Classic economy test line");
    api.flush();
    sol::function drainFn = lua["events_drain"];
    if (!expect(drainFn.valid(), "events_drain binding missing.", outFail)) return false;
    sol::table eventsTbl = drainFn();
    if (!expect(eventsTbl.size() == 1, "events_drain expected one event.", outFail)) return false;
    sol::table e = eventsTbl[1];
    const std::string evtType = e["type"].get_or(std::string());
    const std::string evtPayload = e["payload"].get_or(std::string());
    if (!expect(evtType == "test_event", "events_drain type mismatch.", outFail)) return false;
    if (!expect(evtPayload == "payload", "events_drain payload mismatch.", outFail)) return false;

    // Core mode/start/quit bindings.
    sol::function getStartedFn = lua["get_has_started_game"];
    sol::function setStartedFn = lua["set_has_started_game"];
    sol::function requestQuitFn = lua["request_quit"];
    sol::function requestRestartFn = lua["request_restart_to_menu"];
    sol::function consumeBootFn = lua["consume_boot_menu_screen"];
    sol::function startNewGameFn = lua["start_new_game"];
    sol::function classicIncomeFn = lua["classic_award_round_income"];
    if (!expect(getStartedFn.valid(), "get_has_started_game binding missing.", outFail)) return false;
    if (!expect(setStartedFn.valid(), "set_has_started_game binding missing.", outFail)) return false;
    if (!expect(requestQuitFn.valid(), "request_quit binding missing.", outFail)) return false;
    if (!expect(requestRestartFn.valid(), "request_restart_to_menu binding missing.", outFail)) return false;
    if (!expect(consumeBootFn.valid(), "consume_boot_menu_screen binding missing.", outFail)) return false;
    if (!expect(startNewGameFn.valid(), "start_new_game binding missing.", outFail)) return false;
    if (!expect(classicIncomeFn.valid(), "classic_award_round_income binding missing.", outFail)) return false;
    {
        sol::protected_function_result r = getStartedFn();
        if (!expect(r.valid(), "get_has_started_game call failed.", outFail)) return false;
        const bool startedNow = r.get<bool>();
        if (!expect(startedNow == false, "get_has_started_game initial value mismatch.", outFail)) return false;
    }
    setStartedFn(true);
    {
        sol::protected_function_result r = getStartedFn();
        if (!expect(r.valid(), "get_has_started_game call failed after set.", outFail)) return false;
        const bool startedNow = r.get<bool>();
        if (!expect(startedNow == true, "set_has_started_game did not persist value.", outFail)) return false;
    }
    requestQuitFn();
    if (!expect(quitRequested, "request_quit callback did not run.", outFail)) return false;

    services.bootMenuScreen = "video";
    {
        sol::protected_function_result r = consumeBootFn();
        if (!expect(r.valid(), "consume_boot_menu_screen call failed.", outFail)) return false;
        if (!expect(r.get<std::string>() == "video", "consume_boot_menu_screen should return current boot screen.", outFail)) return false;
    }
    {
        sol::protected_function_result r = consumeBootFn();
        if (!expect(r.valid(), "consume_boot_menu_screen second call failed.", outFail)) return false;
        if (!expect(r.get<std::string>().empty(), "consume_boot_menu_screen should clear consumed value.", outFail)) return false;
    }

    // Capturing units should stay tile-blocking and visible to scripts as captureInProgress.
    const int idB = units[1].id;
    units[1].captureInProgress = true;
    sol::table captured = snapFn(idB);
    if (!expect(captured.valid(), "world_get_unit_snapshot returned invalid table for capture test.", outFail)) return false;
    const bool capFlag = captured["captureInProgress"].get_or(false);
    const bool capBlocks = captured["blocksTile"].get_or(false);
    const bool capAlive = captured["alive"].get_or(true);
    if (!expect(capFlag, "captureInProgress flag missing in unit snapshot.", outFail)) return false;
    if (!expect(capBlocks, "captureInProgress unit should still block tiles.", outFail)) return false;
    if (!expect(!capAlive, "captureInProgress unit should not be marked alive for combat targeting.", outFail)) return false;

    // New game command should reset world state and switch mode.
    world.addMoney(777);
    world.addToBench("bulbasaur", 5);
    startNewGameFn("adventure");
    api.flush();
    if (!expect(services.gameMode == "adventure", "start_new_game did not switch mode.", outFail)) return false;
    if (!expect(services.hasStartedGame, "start_new_game should mark game started.", outFail)) return false;
    if (!expect(world.getPokemons().empty(), "start_new_game did not clear board units.", outFail)) return false;
    if (!expect(world.getBenchPokemons().empty(), "start_new_game did not clear bench units.", outFail)) return false;
    if (!expect(world.getMoney() == cfg.startingCash, "start_new_game did not reset money.", outFail)) return false;

    // Starting classic mode should use classic starting gold.
    startNewGameFn("classic");
    api.flush();
    if (!expect(services.gameMode == "classic", "start_new_game classic did not switch mode.", outFail)) return false;
    if (!expect(world.getMoney() == cfg.classicStartingGold, "classic mode did not use classic starting gold.", outFail)) return false;

    // Classic round income: base + capped interest + streak scaling.
    world.spendMoney(world.getMoney());
    world.addMoney(50); // ensures max interest path with default config
    {
        sol::table income = classicIncomeFn(true);
        if (!expect(income.valid(), "classic_award_round_income did not return table.", outFail)) return false;
        const int base = income["base"].get_or(-1);
        const int interest = income["interest"].get_or(-1);
        const int streak = income["streak"].get_or(-1);
        const int total = income["total"].get_or(-1);
        const int winStreak = income["win_streak"].get_or(-1);
        if (!expect(base == cfg.classicBaseIncome, "classic income base mismatch.", outFail)) return false;
        if (!expect(interest == cfg.classicInterestCap, "classic income interest mismatch.", outFail)) return false;
        if (!expect(streak == 0, "classic income streak bonus should be 0 on first win.", outFail)) return false;
        if (!expect(total == (base + interest + streak), "classic income total mismatch.", outFail)) return false;
        if (!expect(winStreak == 1, "classic win streak should be 1 after first win.", outFail)) return false;
    }
    {
        sol::table income = classicIncomeFn(true);
        const int streak = income["streak"].get_or(-1);
        const int winStreak = income["win_streak"].get_or(-1);
        if (!expect(streak == cfg.classicStreakBonus2To3, "classic streak bonus mismatch at streak 2.", outFail)) return false;
        if (!expect(winStreak == 2, "classic win streak should be 2 after second win.", outFail)) return false;
    }

    return true;
}
