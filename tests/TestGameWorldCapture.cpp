#include <algorithm>
#include <string>

#include "engine/core/Paths.h"

#include "game/GameConfig.h"
#include "game/GameWorld.h"
#include "game/PokemonInstance.h"
#include "game/config/GameDataDb.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

PokemonInstance makeCaptureTarget(const std::string& name,
                                  PokemonSide side,
                                  bool alive,
                                  bool fainting,
                                  bool captureInProgress,
                                  int level,
                                  int maxHp,
                                  int hp) {
    PokemonInstance unit;
    unit.id = PokemonInstance::getNextUnitID();
    unit.name = name;
    unit.side = side;
    unit.alive = alive;
    unit.fainting = fainting;
    unit.captureInProgress = captureInProgress;
    unit.level = std::max(1, level);
    unit.baseHp = std::max(1, maxHp);
    unit.maxHP = std::max(1, maxHp);
    unit.hp = std::max(0, std::min(unit.maxHP, hp));
    unit.position = glm::vec3(1.0f, 0.0f, -2.0f);
    return unit;
}

bool loadPokemonConfig(GameDataDb& db, std::string& outFail) {
    const std::string pokemonPath = engine::paths::data("config/pokemon_config.json");
    if (db.pokemon.loadConfig(pokemonPath, nullptr)) return true;
    outFail = "Failed to load pokemon config: " + pokemonPath;
    return false;
}

void runUpdates(GameWorld& world, int steps, float dt) {
    for (int i = 0; i < steps; ++i) {
        world.update(dt);
    }
}

}  // namespace

bool test_gameworld_capture_preconditions(std::string& outFail) {
    GameConfigData cfg;
    cfg.captureMinChance = 1.0f;
    cfg.captureMaxChance = 1.0f;

    GameWorld world(cfg);
    world.setRenderEnabled(false);

    GameDataDb db;
    if (!loadPokemonConfig(db, outFail)) return false;
    world.setData(&db);

    PokemonInstance player = makeCaptureTarget("bulbasaur", PokemonSide::Player, true, false, false, 2, 90, 75);
    PokemonInstance enemyInProgress = makeCaptureTarget("bulbasaur", PokemonSide::Enemy, true, false, true, 2, 90, 75);
    PokemonInstance enemyGone = makeCaptureTarget("bulbasaur", PokemonSide::Enemy, false, false, false, 2, 90, 0);
    PokemonInstance enemyValid = makeCaptureTarget("bulbasaur", PokemonSide::Enemy, true, false, false, 2, 90, 75);

    const int playerId = player.id;
    const int inProgressId = enemyInProgress.id;
    const int goneId = enemyGone.id;
    const int validId = enemyValid.id;

    world.getPokemons().push_back(player);
    world.getPokemons().push_back(enemyInProgress);
    world.getPokemons().push_back(enemyGone);
    world.getPokemons().push_back(enemyValid);

    if (!expect(!world.startCaptureAttempt(-1, 1.0f, nullptr),
                "startCaptureAttempt should fail for unknown target id.", outFail)) return false;
    if (!expect(!world.startCaptureAttempt(playerId, 1.0f, nullptr),
                "startCaptureAttempt should reject player-side targets.", outFail)) return false;
    if (!expect(!world.startCaptureAttempt(inProgressId, 1.0f, nullptr),
                "startCaptureAttempt should reject already-capturing targets.", outFail)) return false;
    if (!expect(!world.startCaptureAttempt(goneId, 1.0f, nullptr),
                "startCaptureAttempt should reject dead non-fainting targets.", outFail)) return false;

    world.setData(nullptr);
    if (!expect(!world.startCaptureAttempt(validId, 1.0f, nullptr),
                "startCaptureAttempt should fail without GameDataDb.", outFail)) return false;

    return true;
}

bool test_gameworld_capture_success_resolution(std::string& outFail) {
    GameConfigData cfg;
    cfg.captureMinChance = 1.0f;
    cfg.captureMaxChance = 1.0f;
    cfg.captureAttemptSec = 0.2f;

    GameWorld world(cfg);
    world.setRenderEnabled(false);

    GameDataDb db;
    if (!loadPokemonConfig(db, outFail)) return false;
    world.setData(&db);

    PokemonInstance enemy = makeCaptureTarget("bulbasaur", PokemonSide::Enemy, true, false, false, 3, 120, 18);
    const int enemyId = enemy.id;
    world.getPokemons().push_back(enemy);

    if (!expect(world.startCaptureAttempt(enemyId, 1.0f, nullptr),
                "Expected capture attempt to start for valid enemy target.", outFail)) return false;
    const PokemonInstance* markedTarget = world.findUnitById(enemyId);
    if (!expect(markedTarget != nullptr && markedTarget->captureInProgress,
                "Capture start should mark target as captureInProgress.", outFail)) return false;

    runUpdates(world, 40, 0.1f);

    if (!expect(world.findUnitById(enemyId) == nullptr,
                "Successful capture should remove target from board roster.", outFail)) return false;
    if (!expect(world.getPokemons().empty(),
                "Board roster should be empty after capturing the only enemy.", outFail)) return false;
    if (!expect(world.getBenchPokemons().size() == 1,
                "Successful capture should add one unit to bench.", outFail)) return false;

    const PokemonInstance& benched = world.getBenchPokemons().front();
    if (!expect(benched.name == "bulbasaur" && benched.side == PokemonSide::Player,
                "Captured unit bench entry should preserve species and become player-side.", outFail)) return false;
    if (!expect(benched.level == 3,
                "Captured unit should preserve level when benched.", outFail)) return false;

    return true;
}

bool test_gameworld_capture_failure_recovery(std::string& outFail) {
    GameConfigData cfg;
    cfg.captureMinChance = 0.0f;
    cfg.captureMaxChance = 0.0f;
    cfg.captureAttemptSec = 0.2f;

    GameWorld world(cfg);
    world.setRenderEnabled(false);

    GameDataDb db;
    if (!loadPokemonConfig(db, outFail)) return false;
    world.setData(&db);

    PokemonInstance enemy = makeCaptureTarget("bulbasaur", PokemonSide::Enemy, false, true, false, 2, 100, 0);
    const int enemyId = enemy.id;
    world.getPokemons().push_back(enemy);

    if (!expect(world.startCaptureAttempt(enemyId, 1.0f, nullptr),
                "Capture attempt should start for fainting enemy target.", outFail)) return false;

    runUpdates(world, 40, 0.1f);

    if (!expect(world.getBenchPokemons().empty(),
                "Failed capture should not add units to bench.", outFail)) return false;
    PokemonInstance* recovered = world.findUnitById(enemyId);
    if (!expect(recovered != nullptr,
                "Failed capture should keep target unit on board.", outFail)) return false;
    if (!expect(!recovered->captureInProgress && recovered->captureScale == 1.0f && recovered->captureTintStrength == 0.0f,
                "Failed capture should clear capture visual/interaction flags.", outFail)) return false;
    if (!expect(recovered->alive && !recovered->fainting && recovered->hp >= 1,
                "Failed capture should restore fainting target to alive state with minimum HP.", outFail)) return false;

    return true;
}
