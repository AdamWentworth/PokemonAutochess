#include <algorithm>
#include <cmath>
#include <string>

#include "game/GameConfig.h"
#include "game/GameWorld.h"
#include "game/PokemonInstance.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

bool nearf(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

bool near3(const glm::vec3& a, const glm::vec3& b, float eps = 0.0001f) {
    return nearf(a.x, b.x, eps) && nearf(a.y, b.y, eps) && nearf(a.z, b.z, eps);
}

PokemonInstance makeUnit(const std::string& name,
                         PokemonSide side,
                         const glm::vec3& position,
                         int maxHp,
                         int hp,
                         bool alive = true) {
    PokemonInstance unit;
    unit.id = PokemonInstance::getNextUnitID();
    unit.name = name;
    unit.side = side;
    unit.position = position;
    unit.maxHP = std::max(1, maxHp);
    unit.hp = std::max(0, std::min(unit.maxHP, hp));
    unit.alive = alive;
    return unit;
}

}  // namespace

bool test_gameworld_heal_player_units_to_full(std::string& outFail) {
    GameConfigData cfg;
    GameWorld world(cfg);
    world.setRenderEnabled(false);

    PokemonInstance boardPlayer = makeUnit("board_player", PokemonSide::Player, glm::vec3(1.0f, 0.0f, 1.0f), 120, 30, false);
    boardPlayer.fainting = true;
    boardPlayer.faintTimerSec = 2.0f;
    boardPlayer.fadeOutTimerSec = 0.1f;
    boardPlayer.visualScale = 0.2f;
    boardPlayer.captureInProgress = true;
    boardPlayer.captureScale = 0.4f;
    boardPlayer.captureTintStrength = 0.8f;
    boardPlayer.isMoving = true;
    boardPlayer.moveT = 0.3f;
    boardPlayer.attackTimerSec = 2.0f;
    boardPlayer.pendingDamageActive = true;
    boardPlayer.pendingDamageApplied = true;
    boardPlayer.pendingDamageMoveName = "tackle";
    boardPlayer.pendingProjectileActive = true;
    boardPlayer.pendingProjectileSpawned = true;
    boardPlayer.pendingImpactActive = true;
    boardPlayer.pendingImpactApplied = true;
    boardPlayer.leechSeeded = true;
    boardPlayer.leechSeedSourceId = 99;
    boardPlayer.leechSeedTimeLeftSec = 7.0f;
    boardPlayer.leechSeedTickTimerSec = 0.5f;

    PokemonInstance benchPlayer = makeUnit("bench_player", PokemonSide::Player, glm::vec3(-2.0f, 0.0f, 3.0f), 80, 1, false);
    benchPlayer.captureInProgress = true;
    benchPlayer.leechSeeded = true;
    benchPlayer.pendingDamageActive = true;
    benchPlayer.pendingProjectileActive = true;
    benchPlayer.pendingImpactActive = true;

    PokemonInstance enemy = makeUnit("enemy", PokemonSide::Enemy, glm::vec3(5.0f, 0.0f, 5.0f), 90, 17, false);
    enemy.captureInProgress = true;
    enemy.isMoving = true;

    const int boardPlayerId = boardPlayer.id;
    const int benchPlayerId = benchPlayer.id;
    const int enemyId = enemy.id;

    world.getPokemons().push_back(boardPlayer);
    world.getBenchPokemons().push_back(benchPlayer);
    world.getPokemons().push_back(enemy);

    world.healPlayerUnitsToFull();

    const PokemonInstance* healedBoard = world.findUnitById(boardPlayerId);
    const PokemonInstance* healedBench = world.findUnitById(benchPlayerId);
    const PokemonInstance* untouchedEnemy = world.findUnitById(enemyId);

    if (!expect(healedBoard != nullptr && healedBench != nullptr && untouchedEnemy != nullptr,
                "Expected all units to remain resolvable after healing.", outFail)) return false;

    const auto verifyHealedPlayer = [&](const PokemonInstance* u, const char* scope) -> bool {
        if (!expect(u->alive, std::string(scope) + " should be alive after heal.", outFail)) return false;
        if (!expect(!u->fainting, std::string(scope) + " should not be fainting after heal.", outFail)) return false;
        if (!expect(nearf(u->faintTimerSec, 0.0f), std::string(scope) + " faintTimerSec should reset.", outFail)) return false;
        if (!expect(nearf(u->fadeOutTimerSec, 0.0f), std::string(scope) + " fadeOutTimerSec should reset.", outFail)) return false;
        if (!expect(nearf(u->visualScale, 1.0f), std::string(scope) + " visualScale should reset to 1.", outFail)) return false;
        if (!expect(!u->captureInProgress && nearf(u->captureScale, 1.0f) && nearf(u->captureTintStrength, 0.0f),
                    std::string(scope) + " capture flags should reset.", outFail)) return false;
        if (!expect(!u->isMoving && nearf(u->moveT, 1.0f), std::string(scope) + " movement flags should reset.", outFail)) return false;
        if (!expect(nearf(u->attackTimerSec, 0.0f), std::string(scope) + " attackTimerSec should reset.", outFail)) return false;
        if (!expect(!u->pendingDamageActive && !u->pendingDamageApplied && u->pendingDamageMoveName.empty(),
                    std::string(scope) + " pending damage flags should reset.", outFail)) return false;
        if (!expect(!u->pendingProjectileActive && !u->pendingProjectileSpawned,
                    std::string(scope) + " pending projectile flags should reset.", outFail)) return false;
        if (!expect(!u->pendingImpactActive && !u->pendingImpactApplied,
                    std::string(scope) + " pending impact flags should reset.", outFail)) return false;
        if (!expect(!u->leechSeeded && u->leechSeedSourceId == -1 &&
                    nearf(u->leechSeedTimeLeftSec, 0.0f) && nearf(u->leechSeedTickTimerSec, 0.0f),
                    std::string(scope) + " leech-seed flags should reset.", outFail)) return false;
        if (!expect(u->hp == u->maxHP, std::string(scope) + " HP should be restored to max.", outFail)) return false;
        return true;
    };

    if (!verifyHealedPlayer(healedBoard, "Board player")) return false;
    if (!verifyHealedPlayer(healedBench, "Bench player")) return false;

    if (!expect(!untouchedEnemy->alive && untouchedEnemy->hp == 17 && untouchedEnemy->captureInProgress && untouchedEnemy->isMoving,
                "Enemy unit should remain unchanged by player-only heal.", outFail)) return false;

    return true;
}

bool test_gameworld_capture_restore_player_positions(std::string& outFail) {
    GameConfigData cfg;
    GameWorld world(cfg);
    world.setRenderEnabled(false);

    PokemonInstance boardPlayer = makeUnit("board_player", PokemonSide::Player, glm::vec3(1.0f, 0.0f, 1.0f), 100, 100, true);
    PokemonInstance benchPlayer = makeUnit("bench_player", PokemonSide::Player, glm::vec3(-2.0f, 0.0f, 3.0f), 100, 100, true);
    PokemonInstance enemy = makeUnit("enemy", PokemonSide::Enemy, glm::vec3(4.0f, 0.0f, -1.0f), 100, 100, true);

    const glm::vec3 boardStart = boardPlayer.position;
    const glm::vec3 benchStart = benchPlayer.position;
    const int boardId = boardPlayer.id;
    const int benchId = benchPlayer.id;
    const int enemyId = enemy.id;

    world.getPokemons().push_back(boardPlayer);
    world.getBenchPokemons().push_back(benchPlayer);
    world.getPokemons().push_back(enemy);

    world.capturePlayerPositionsForBattle();

    PokemonInstance* board = world.findUnitById(boardId);
    PokemonInstance* bench = world.findUnitById(benchId);
    PokemonInstance* enemyRef = world.findUnitById(enemyId);
    if (!expect(board != nullptr && bench != nullptr && enemyRef != nullptr,
                "Expected all units to be available before restore.", outFail)) return false;

    board->position = glm::vec3(10.0f, 0.0f, 10.0f);
    board->rotation.y = 15.0f;
    board->isMoving = true;
    board->moveT = 0.2f;
    board->committedDest = {3, 4};
    board->moveFrom = glm::vec3(9.0f, 0.0f, 9.0f);
    board->moveTo = glm::vec3(11.0f, 0.0f, 11.0f);

    bench->position = glm::vec3(-8.0f, 0.0f, -3.0f);
    bench->rotation.y = 20.0f;
    bench->isMoving = true;
    bench->moveT = 0.4f;
    bench->committedDest = {1, 1};
    bench->moveFrom = glm::vec3(-7.0f, 0.0f, -2.0f);
    bench->moveTo = glm::vec3(-9.0f, 0.0f, -4.0f);

    enemyRef->position = glm::vec3(20.0f, 0.0f, 20.0f);

    world.restorePlayerPositionsAfterBattle();

    if (!expect(near3(board->position, boardStart), "Board player position should restore to captured value.", outFail)) return false;
    if (!expect(near3(bench->position, benchStart), "Bench player position should restore to captured value.", outFail)) return false;
    if (!expect(nearf(board->rotation.y, 180.0f) && nearf(bench->rotation.y, 180.0f),
                "Player units should face backline after restore.", outFail)) return false;
    if (!expect(!board->isMoving && !bench->isMoving && nearf(board->moveT, 1.0f) && nearf(bench->moveT, 1.0f),
                "Restore should clear movement state for players.", outFail)) return false;
    if (!expect(board->committedDest == glm::ivec2(-1, -1) && bench->committedDest == glm::ivec2(-1, -1),
                "Restore should clear committed destinations.", outFail)) return false;
    if (!expect(near3(board->moveFrom, board->position) && near3(board->moveTo, board->position) &&
                near3(bench->moveFrom, bench->position) && near3(bench->moveTo, bench->position),
                "Restore should snap move endpoints to restored positions.", outFail)) return false;
    if (!expect(near3(enemyRef->position, glm::vec3(20.0f, 0.0f, 20.0f)),
                "Restore should not alter enemy positions.", outFail)) return false;

    board->position = glm::vec3(30.0f, 0.0f, 30.0f);
    bench->position = glm::vec3(-30.0f, 0.0f, -30.0f);
    world.restorePlayerPositionsAfterBattle();

    if (!expect(near3(board->position, glm::vec3(30.0f, 0.0f, 30.0f)) &&
                near3(bench->position, glm::vec3(-30.0f, 0.0f, -30.0f)),
                "Restore should be a no-op when no captured positions are available.", outFail)) return false;

    return true;
}

bool test_gameworld_handle_unit_faint_state_reset(std::string& outFail) {
    GameConfigData cfg;
    cfg.faintFadeSec = 0.6f;

    GameWorld world(cfg);
    world.setRenderEnabled(false);

    PokemonInstance target = makeUnit("enemy", PokemonSide::Enemy, glm::vec3(0.0f), 100, 37, true);
    target.isMoving = true;
    target.attackTimerSec = 2.5f;
    target.attackAnimSpeed = 2.0f;
    target.animAttack1Index = 7;
    target.currentAttackAnimIndex = 9;
    target.pendingAttackAfterLanding = true;
    target.queuedAttackDurationSec = 1.2f;
    target.queuedAttackAnimIndex = 13;
    target.chainedFastMove = "scratch";
    target.fastChainTimerSec = 0.7f;
    target.pendingDamageActive = true;
    target.pendingDamageApplied = true;
    target.pendingDamageTargetId = 33;
    target.pendingDamageAmount = 18;
    target.pendingDamageHitTimeSec = 0.4f;
    target.pendingDamageMoveName = "tackle";
    target.animIndexCache["atk"] = 2;
    target.leechSeeded = true;
    target.leechSeedSourceId = 8;
    target.leechSeedTimeLeftSec = 4.0f;
    target.leechSeedTickTimerSec = 0.2f;

    const int targetId = target.id;
    world.getPokemons().push_back(target);

    PokemonInstance* resolved = world.findUnitById(targetId);
    if (!expect(resolved != nullptr, "Expected to resolve target before faint.", outFail)) return false;

    world.handleUnitFaint(*resolved);

    if (!expect(!resolved->alive && resolved->hp == 0, "Faint should set alive=false and hp=0.", outFail)) return false;
    if (!expect(!resolved->isMoving && nearf(resolved->attackTimerSec, 0.0f),
                "Faint should stop movement and clear attack timer.", outFail)) return false;
    if (!expect(nearf(resolved->attackAnimSpeed, 1.0f) && resolved->currentAttackAnimIndex == resolved->animAttack1Index,
                "Faint should reset attack animation state.", outFail)) return false;
    if (!expect(!resolved->pendingAttackAfterLanding && nearf(resolved->queuedAttackDurationSec, 0.0f) &&
                resolved->queuedAttackAnimIndex == -1,
                "Faint should clear queued attack state.", outFail)) return false;
    if (!expect(resolved->chainedFastMove.empty() && nearf(resolved->fastChainTimerSec, 0.0f),
                "Faint should clear fast-chain state.", outFail)) return false;
    if (!expect(!resolved->pendingDamageActive && !resolved->pendingDamageApplied &&
                resolved->pendingDamageTargetId == -1 && resolved->pendingDamageAmount == 0 &&
                nearf(resolved->pendingDamageHitTimeSec, 0.0f) && resolved->pendingDamageMoveName.empty(),
                "Faint should clear pending damage state.", outFail)) return false;
    if (!expect(resolved->animIndexCache.empty(), "Faint should clear animation index cache.", outFail)) return false;
    if (!expect(!resolved->leechSeeded && resolved->leechSeedSourceId == -1 &&
                nearf(resolved->leechSeedTimeLeftSec, 0.0f) && nearf(resolved->leechSeedTickTimerSec, 0.0f),
                "Faint should clear leech-seed status.", outFail)) return false;
    if (!expect(resolved->fainting && nearf(resolved->faintTimerSec, 0.0f),
                "Faint should enter fainting visual state.", outFail)) return false;
    if (!expect(nearf(resolved->fadeOutSec, cfg.faintFadeSec) && nearf(resolved->fadeOutTimerSec, cfg.faintFadeSec),
                "Faint should initialize fade timers from config.", outFail)) return false;
    if (!expect(nearf(resolved->visualScale, 1.0f), "Faint should reset visualScale to 1.", outFail)) return false;

    world.handleUnitFaint(*resolved);
    if (!expect(!resolved->alive && resolved->hp == 0 && resolved->fainting,
                "Calling handleUnitFaint on an already dead unit should be a no-op.", outFail)) return false;

    return true;
}
