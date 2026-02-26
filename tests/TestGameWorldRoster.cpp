#include <algorithm>
#include <cmath>
#include <exception>
#include <string>

#include "engine/core/Paths.h"
#include "engine/utils/ResourceManager.h"

#include "game/GameConfig.h"
#include "game/GameWorld.h"
#include "game/config/GameDataDb.h"

namespace {

bool nearf(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

bool near3(const glm::vec3& a, const glm::vec3& b, float eps = 0.0001f) {
    return nearf(a.x, b.x, eps) && nearf(a.y, b.y, eps) && nearf(a.z, b.z, eps);
}

}  // namespace

bool test_gameworld_spawn_bench_flow(std::string& outFail) {
    GameConfigData cfg;
    GameDataDb db;

    const std::string pokemonPath = engine::paths::data("config/pokemon_config.json");
    const std::string movesPath = engine::paths::data("config/moves_config.json");
    if (!db.pokemon.loadConfig(pokemonPath, nullptr)) {
        outFail = "Failed to load pokemon config: " + pokemonPath;
        return false;
    }
    if (!db.moves.loadConfig(movesPath, nullptr)) {
        outFail = "Failed to load moves config: " + movesPath;
        return false;
    }

    GameWorld world(cfg);
    world.setData(&db);
    world.setRenderEnabled(false);

    const glm::vec3 enemyPos(3.5f, 0.0f, -1.25f);
    world.spawnPokemon("bulbasaur", enemyPos, PokemonSide::Enemy, 3);

    auto& board = world.getPokemons();
    if (board.size() != 1) {
        outFail = "spawnPokemon should add exactly one unit.";
        return false;
    }
    const PokemonInstance& enemy = board.front();
    if (enemy.side != PokemonSide::Enemy) {
        outFail = "Enemy spawn should set side=Enemy.";
        return false;
    }
    if (!near3(enemy.position, enemyPos)) {
        outFail = "Enemy spawn position mismatch.";
        return false;
    }
    if (!nearf(enemy.rotation.y, 0.0f)) {
        outFail = "Enemy spawn should face forward (rotation.y = 0).";
        return false;
    }
    if (enemy.level != 3) {
        outFail = "Enemy spawn level mismatch.";
        return false;
    }

    world.spawnPokemonAtGrid("rattata", 2, 3, PokemonSide::Player, 1);
    if (board.size() != 2) {
        outFail = "spawnPokemonAtGrid should add one board unit.";
        return false;
    }
    const PokemonInstance& playerGrid = board.back();
    if (playerGrid.side != PokemonSide::Player) {
        outFail = "Grid spawn should set side=Player.";
        return false;
    }
    if (!nearf(playerGrid.rotation.y, 180.0f)) {
        outFail = "Player grid spawn should face backline (rotation.y = 180).";
        return false;
    }
    const glm::ivec2 cell = world.worldToGrid(playerGrid.position);
    if (cell.x != 2 || cell.y != 3) {
        outFail = "spawnPokemonAtGrid world/grid conversion mismatch.";
        return false;
    }

    world.addToBench("charmander", 5);
    auto& bench = world.getBenchPokemons();
    if (bench.size() != 1) {
        outFail = "addToBench should add exactly one bench unit.";
        return false;
    }
    const PokemonInstance& benched = bench.front();
    if (benched.side != PokemonSide::Player || !nearf(benched.rotation.y, 180.0f)) {
        outFail = "Benched unit should be player-side and face backline.";
        return false;
    }
    if (benched.level != 5) {
        outFail = "Benched level mismatch.";
        return false;
    }
    const float cellSize = world.getBoardCellSize();
    const int benchSlots = std::max(1, cfg.benchSlots);
    const float totalWidth = benchSlots * cellSize;
    const float startX = -totalWidth * 0.5f;
    const float startZ = (cfg.rows * cellSize) * 0.5f + 0.5f;
    const glm::vec3 expectedBenchPos(startX + cellSize * 0.5f, 0.0f, startZ + cellSize * 0.5f);
    if (!near3(benched.position, expectedBenchPos)) {
        outFail = "Benched unit position mismatch for slot 0.";
        return false;
    }

    const std::size_t boardBeforeInvalid = board.size();
    const std::size_t benchBeforeInvalid = bench.size();
    world.spawnPokemon("not_a_species", glm::vec3(0.0f), PokemonSide::Enemy, 1);
    world.addToBench("not_a_species", 1);
    if (board.size() != boardBeforeInvalid || bench.size() != benchBeforeInvalid) {
        outFail = "Invalid species should not change board/bench counts.";
        return false;
    }

    return true;
}

bool test_gameworld_nonrender_with_resources_skips_model_load(std::string& outFail) {
    GameConfigData cfg;
    GameDataDb db;

    const std::string pokemonPath = engine::paths::data("config/pokemon_config.json");
    const std::string movesPath = engine::paths::data("config/moves_config.json");
    if (!db.pokemon.loadConfig(pokemonPath, nullptr)) {
        outFail = "Failed to load pokemon config: " + pokemonPath;
        return false;
    }
    if (!db.moves.loadConfig(movesPath, nullptr)) {
        outFail = "Failed to load moves config: " + movesPath;
        return false;
    }

    GameWorld world(cfg);
    world.setData(&db);
    world.setRenderEnabled(false);

    ResourceManager resources;
    world.setResources(&resources);

    try {
        world.spawnPokemon("bulbasaur", glm::vec3(0.0f, 0.0f, 0.0f), PokemonSide::Player, 3);
    } catch (const std::exception& ex) {
        outFail = std::string("spawnPokemon should not throw in non-render mode: ") + ex.what();
        return false;
    }

    const auto& board = world.getPokemons();
    if (board.empty()) {
        outFail = "spawnPokemon should still create units in non-render mode.";
        return false;
    }
    if (board.back().model != nullptr) {
        outFail = "non-render spawn should skip OpenGL model attachment.";
        return false;
    }

    try {
        world.addToBench("charmander", 2);
    } catch (const std::exception& ex) {
        outFail = std::string("addToBench should not throw in non-render mode: ") + ex.what();
        return false;
    }

    const auto& bench = world.getBenchPokemons();
    if (bench.empty()) {
        outFail = "addToBench should still create bench units in non-render mode.";
        return false;
    }
    if (bench.back().model != nullptr) {
        outFail = "non-render bench add should skip OpenGL model attachment.";
        return false;
    }

    return true;
}

bool test_gameworld_backend_render_mode_skips_legacy_model_load(std::string& outFail) {
    GameConfigData cfg;
    GameDataDb db;

    const std::string pokemonPath = engine::paths::data("config/pokemon_config.json");
    const std::string movesPath = engine::paths::data("config/moves_config.json");
    if (!db.pokemon.loadConfig(pokemonPath, nullptr)) {
        outFail = "Failed to load pokemon config: " + pokemonPath;
        return false;
    }
    if (!db.moves.loadConfig(movesPath, nullptr)) {
        outFail = "Failed to load moves config: " + movesPath;
        return false;
    }

    GameWorld world(cfg);
    world.setData(&db);
    world.setRenderEnabled(true);

    ResourceManager resources;
    world.setResources(&resources);

    try {
        world.spawnPokemon("bulbasaur", glm::vec3(0.0f, 0.0f, 0.0f), PokemonSide::Player, 3);
    } catch (const std::exception& ex) {
        outFail = std::string("backend render-mode spawn should not throw: ") + ex.what();
        return false;
    }

    const auto& board = world.getPokemons();
    if (board.empty()) {
        outFail = "spawnPokemon should still create units in backend render mode.";
        return false;
    }
    if (board.back().model != nullptr) {
        outFail = "backend render mode should skip legacy OpenGL model attachment.";
        return false;
    }

    try {
        world.addToBench("charmander", 2);
    } catch (const std::exception& ex) {
        outFail = std::string("backend render-mode bench add should not throw: ") + ex.what();
        return false;
    }

    const auto& bench = world.getBenchPokemons();
    if (bench.empty()) {
        outFail = "addToBench should still create bench units in backend render mode.";
        return false;
    }
    if (bench.back().model != nullptr) {
        outFail = "backend render mode should skip legacy bench model attachment.";
        return false;
    }

    return true;
}
