#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

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

bool nearf(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

bool near3(const glm::vec3& a, const glm::vec3& b, float eps = 0.0001f) {
    return nearf(a.x, b.x, eps) && nearf(a.y, b.y, eps) && nearf(a.z, b.z, eps);
}

PokemonInstance makeUnit(const std::string& name,
                         PokemonSide side,
                         const glm::vec3& pos,
                         bool alive = true,
                         bool captureInProgress = false) {
    PokemonInstance unit;
    unit.id = PokemonInstance::getNextUnitID();
    unit.name = name;
    unit.side = side;
    unit.position = pos;
    unit.alive = alive;
    unit.captureInProgress = captureInProgress;
    unit.maxHP = 100;
    unit.hp = alive ? 100 : 0;
    return unit;
}

bool loadTypeCountData(GameDataDb& db, std::string& outFail) {
    const std::string pokemonPath = engine::paths::data("config/pokemon_config.json");
    if (!db.pokemon.loadConfig(pokemonPath, nullptr)) {
        outFail = "Failed to load pokemon config: " + pokemonPath;
        return false;
    }

    const std::string evolutionPath = engine::paths::data("config/evolution_config.json");
    if (!db.evolution.loadConfig(evolutionPath, nullptr)) {
        outFail = "Failed to load evolution config: " + evolutionPath;
        return false;
    }
    return true;
}

}  // namespace

bool test_gameworld_type_line_counts(std::string& outFail) {
    GameConfigData cfg;
    GameDataDb db;
    if (!loadTypeCountData(db, outFail)) return false;

    GameWorld world(cfg);
    world.setRenderEnabled(false);
    world.setData(&db);

    world.getPokemons().push_back(makeUnit("bulbasaur", PokemonSide::Player, glm::vec3(0.0f, 0.0f, 0.0f)));
    world.getBenchPokemons().push_back(makeUnit("ivysaur", PokemonSide::Player, glm::vec3(1.0f, 0.0f, 0.0f)));

    world.getPokemons().push_back(makeUnit("charmander", PokemonSide::Player, glm::vec3(2.0f, 0.0f, 0.0f)));
    world.getBenchPokemons().push_back(makeUnit("charmeleon", PokemonSide::Player, glm::vec3(3.0f, 0.0f, 0.0f)));

    world.getPokemons().push_back(makeUnit("caterpie", PokemonSide::Player, glm::vec3(4.0f, 0.0f, 0.0f)));
    world.getBenchPokemons().push_back(makeUnit("weedle", PokemonSide::Player, glm::vec3(5.0f, 0.0f, 0.0f)));

    world.getPokemons().push_back(makeUnit("squirtle", PokemonSide::Player, glm::vec3(6.0f, 0.0f, 0.0f)));
    world.getBenchPokemons().push_back(makeUnit("wartortle", PokemonSide::Player, glm::vec3(7.0f, 0.0f, 0.0f)));

    world.getPokemons().push_back(makeUnit("pidgey", PokemonSide::Player, glm::vec3(8.0f, 0.0f, 0.0f)));

    // Enemies should be ignored by player type-line counting.
    world.getPokemons().push_back(makeUnit("weedle", PokemonSide::Enemy, glm::vec3(9.0f, 0.0f, 0.0f)));
    world.getBenchPokemons().push_back(makeUnit("metapod", PokemonSide::Enemy, glm::vec3(10.0f, 0.0f, 0.0f)));

    const std::vector<GameWorld::TypeLineCount> counts = world.getPlayerTypeLineCounts();
    if (!expect(counts.size() == 7, "Expected 7 type entries from configured player roster.", outFail)) return false;

    std::unordered_map<std::string, int> actual;
    for (const auto& row : counts) {
        actual[row.type] = row.uniqueLineCount;
    }

    const std::unordered_map<std::string, int> expected{
        {"bug", 2},
        {"poison", 2},
        {"fire", 1},
        {"grass", 1},
        {"water", 1},
        {"normal", 1},
        {"flying", 1},
    };

    if (!expect(actual == expected, "Type-line counts mismatch for mixed board/bench evolution lines.", outFail)) return false;

    for (std::size_t i = 1; i < counts.size(); ++i) {
        const auto& prev = counts[i - 1];
        const auto& curr = counts[i];
        const bool countOrderOk = prev.uniqueLineCount >= curr.uniqueLineCount;
        if (!expect(countOrderOk, "Type counts should be sorted descending by uniqueLineCount.", outFail)) return false;
        if (prev.uniqueLineCount == curr.uniqueLineCount) {
            const bool alphaOrderOk = prev.type <= curr.type;
            if (!expect(alphaOrderOk, "Type counts should tie-break by type name ascending.", outFail)) return false;
        }
    }

    GameWorld noDataWorld(cfg);
    noDataWorld.setRenderEnabled(false);
    noDataWorld.getPokemons().push_back(makeUnit("bulbasaur", PokemonSide::Player, glm::vec3(0.0f)));
    if (!expect(noDataWorld.getPlayerTypeLineCounts().empty(),
                "getPlayerTypeLineCounts should return empty when GameDataDb is not set.", outFail)) return false;

    return true;
}

bool test_gameworld_nearest_enemy_position(std::string& outFail) {
    GameConfigData cfg;
    GameWorld world(cfg);
    world.setRenderEnabled(false);

    const PokemonInstance playerA = makeUnit("player_a", PokemonSide::Player, glm::vec3(0.0f, 0.0f, 0.0f));
    const PokemonInstance playerB = makeUnit("player_b", PokemonSide::Player, glm::vec3(10.0f, 0.0f, 0.0f));
    const PokemonInstance enemyNear = makeUnit("enemy_near", PokemonSide::Enemy, glm::vec3(1.0f, 0.0f, 0.0f));
    const PokemonInstance enemyFar = makeUnit("enemy_far", PokemonSide::Enemy, glm::vec3(5.0f, 0.0f, 0.0f));
    const PokemonInstance enemyCaptured = makeUnit("enemy_captured", PokemonSide::Enemy, glm::vec3(0.2f, 0.0f, 0.0f), true, true);
    const PokemonInstance enemyDead = makeUnit("enemy_dead", PokemonSide::Enemy, glm::vec3(0.1f, 0.0f, 0.0f), false, false);

    const int playerAId = playerA.id;
    const int enemyFarId = enemyFar.id;

    world.getPokemons().push_back(playerA);
    world.getPokemons().push_back(playerB);
    world.getPokemons().push_back(enemyNear);
    world.getPokemons().push_back(enemyFar);
    world.getPokemons().push_back(enemyCaptured);
    world.getPokemons().push_back(enemyDead);

    const PokemonInstance* playerRef = world.findUnitById(playerAId);
    if (!expect(playerRef != nullptr, "Expected to resolve player unit by id.", outFail)) return false;
    const glm::vec3 nearestFromPlayer = world.getNearestEnemyPosition(*playerRef);
    if (!expect(near3(nearestFromPlayer, enemyNear.position),
                "Nearest enemy should ignore dead/capturing units and pick closest valid enemy.", outFail)) return false;

    const PokemonInstance* enemyRef = world.findUnitById(enemyFarId);
    if (!expect(enemyRef != nullptr, "Expected to resolve enemy unit by id.", outFail)) return false;
    const glm::vec3 nearestFromEnemy = world.getNearestEnemyPosition(*enemyRef);
    if (!expect(near3(nearestFromEnemy, playerA.position),
                "Nearest enemy query should be side-relative and pick nearest opposite-side unit.", outFail)) return false;

    GameWorld soloWorld(cfg);
    soloWorld.setRenderEnabled(false);
    const PokemonInstance solo = makeUnit("solo", PokemonSide::Player, glm::vec3(2.0f, 0.0f, 3.0f));
    const int soloId = solo.id;
    soloWorld.getPokemons().push_back(solo);

    const PokemonInstance* soloRef = soloWorld.findUnitById(soloId);
    if (!expect(soloRef != nullptr, "Expected to resolve solo unit by id.", outFail)) return false;
    const glm::vec3 nearestSolo = soloWorld.getNearestEnemyPosition(*soloRef);
    if (!expect(near3(nearestSolo, solo.position),
                "Nearest enemy should fall back to unit position when no valid opponents exist.", outFail)) return false;

    return true;
}
