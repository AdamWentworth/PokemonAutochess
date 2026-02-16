#include <string>

#include "game/GameConfig.h"
#include "game/GameWorld.h"
#include "game/PokemonInstance.h"

namespace {

PokemonInstance makeUnit(const std::string& name, int level, int xp, bool onBoard) {
    PokemonInstance unit;
    unit.id = PokemonInstance::getNextUnitID();
    unit.name = name;
    unit.side = PokemonSide::Player;
    unit.alive = true;
    unit.captureInProgress = false;
    unit.level = level;
    unit.xp = xp;
    unit.baseHp = 100;
    unit.baseAttack = 10;
    unit.baseMovementSpeed = 1.0f;
    unit.maxHP = 100;
    unit.hp = 100;
    unit.attack = 10;
    unit.movementSpeed = 1.0f;
    unit.position = onBoard ? glm::vec3(0.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 4.0f);
    return unit;
}

}  // namespace

bool test_gameworld_merge_progression(std::string& outFail) {
    GameConfigData cfg;
    cfg.xpLevelBase = 10;
    cfg.xpLevelGrowth = 1.0f;
    cfg.xpMaxLevel = 10;

    GameWorld world(cfg);
    world.setRenderEnabled(false);

    PokemonInstance board = makeUnit("bulbasaur", 1, 5, true);
    PokemonInstance benchA = makeUnit("bulbasaur", 1, 3, false);
    PokemonInstance benchB = makeUnit("bulbasaur", 1, 2, false);

    const int boardId = board.id;
    const int benchAId = benchA.id;
    const int benchBId = benchB.id;

    world.getPokemons().push_back(board);
    world.getBenchPokemons().push_back(benchA);
    world.getBenchPokemons().push_back(benchB);

    world.mergeTriplesForPlayer();

    if (world.getPokemons().size() != 1 || !world.getBenchPokemons().empty()) {
        outFail = "Expected 3-way merge to leave exactly one player unit.";
        return false;
    }

    PokemonInstance* merged = world.findUnitById(boardId);
    if (!merged) {
        outFail = "Expected highest-XP on-board unit to be merge keeper.";
        return false;
    }
    if (world.findUnitById(benchAId) || world.findUnitById(benchBId)) {
        outFail = "Merged source units should be removed.";
        return false;
    }

    if (merged->level != 2 || merged->xp != 0) {
        outFail = "Merged XP progression should produce level 2 with zero remainder XP.";
        return false;
    }

    return true;
}
