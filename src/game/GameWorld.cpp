// GameWorld.cpp
#include "GameWorld.h"
#include "../engine/utils/ResourceManager.h"
#include "../engine/render/Model.h"
#include "../engine/render/Camera3D.h"
#include "../engine/render/BoardRenderer.h"
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include "PokemonConfigLoader.h"
#include "GameConfig.h"
#include <cmath>

void GameWorld::applyLevelScaling(PokemonInstance& inst, int level) const {
    const auto& cfg = GameConfig::get();
    const int useLevel = (level <= 0) ? cfg.baseLevel : level;

    inst.level = useLevel;

    // multiplier = (1 + perLevelBoost)^(level - 1)
    const float mult = std::pow(1.0f + cfg.perLevelBoost, static_cast<float>(useLevel - 1));

    inst.maxHP         = static_cast<int>(std::round(static_cast<float>(inst.baseHp) * mult));
    inst.hp            = inst.maxHP; // spawn at full health
    inst.attack        = static_cast<int>(std::round(static_cast<float>(inst.baseAttack) * mult));
    inst.movementSpeed = inst.baseMovementSpeed * mult;
}

void GameWorld::spawnPokemon(const std::string& pokemonName,
                             const glm::vec3& startPos,
                             PokemonSide side,
                             int level) {
    const PokemonStats* stats = PokemonConfigLoader::getInstance().getStats(pokemonName);
    if (!stats) {
        std::cerr << "[GameWorld] No config found for Pokémon: " << pokemonName << "\n";
        return;
    }

    std::string path = "assets/models/" + stats->model;
    auto sharedModel = ResourceManager::getInstance().getModel(path);

    PokemonInstance inst;
    inst.id = PokemonInstance::getNextUnitID();
    inst.name = pokemonName;
    inst.model = sharedModel;
    inst.position = startPos;
    inst.rotation = glm::vec3(90.0f, (side == PokemonSide::Player ? 180.0f : 0.0f), 0.0f);
    inst.side = side;

    // Store base (unscaled) stats from config
    inst.baseHp = stats->hp;
    inst.baseAttack = stats->attack;
    inst.baseMovementSpeed = stats->movementSpeed;

    // Apply level scaling (sets level, hp/maxHP, attack, movementSpeed)
    applyLevelScaling(inst, level);

    pokemons.push_back(inst);

    std::cout << "[GameWorld] Spawned " << pokemonName
              << " (ID: " << inst.id
              << ", L" << inst.level
              << ", HP: " << inst.hp << "/" << inst.maxHP
              << ", ATK: " << inst.attack
              << ", SPD: " << inst.movementSpeed << ")\n";
}

glm::vec3 GameWorld::gridToWorld(int col, int row) const {
    const auto& cfg = GameConfig::get();
    float boardOriginX = -((cfg.cols * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    float boardOriginZ = -((cfg.rows * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    return { boardOriginX + col * cfg.cellSize, 0.0f, boardOriginZ + row * cfg.cellSize };
}

void GameWorld::spawnPokemonAtGrid(const std::string& pokemonName,
                                   int col, int row,
                                   PokemonSide side,
                                   int level) {
    spawnPokemon(pokemonName, gridToWorld(col, row), side, level);
}

void GameWorld::addToBench(const std::string& pokemonName) {
    const PokemonStats* stats = PokemonConfigLoader::getInstance().getStats(pokemonName);
    if (!stats) {
        std::cerr << "[GameWorld] No config found for Pokémon: " << pokemonName << "\n";
        return;
    }

    std::string path = "assets/models/" + stats->model;
    auto sharedModel = ResourceManager::getInstance().getModel(path);

    PokemonInstance inst;
    inst.id = PokemonInstance::getNextUnitID();
    inst.name = pokemonName;
    inst.model = sharedModel;
    inst.rotation = glm::vec3(90.0f, 180.0f, 0.0f);
    inst.side = PokemonSide::Player;

    // base stats
    inst.baseHp = stats->hp;
    inst.baseAttack = stats->attack;
    inst.baseMovementSpeed = stats->movementSpeed;

    // Apply default level scaling for bench (uses baseLevel)
    applyLevelScaling(inst, /*level*/ -1);

    int slot = static_cast<int>(benchPokemons.size());
    float spacing = 1.2f;
    float x = (slot - 4) * spacing + spacing / 2.0f;
    float z = 4.5f; // legacy bench position
    inst.position = glm::vec3(x, 0.0f, z);

    benchPokemons.push_back(inst);

    std::cout << "[GameWorld] Benched " << pokemonName
              << " (ID: " << inst.id
              << " L" << inst.level
              << ", slot " << slot << ")\n";
}

const PokemonInstance* GameWorld::getPokemonByName(const std::string& name) const {
    for (const auto& p : pokemons) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

std::vector<PokemonInstance>& GameWorld::getPokemons() { return pokemons; }
std::vector<PokemonInstance>& GameWorld::getBenchPokemons() { return benchPokemons; }

void GameWorld::drawAll(const Camera3D& camera, BoardRenderer& boardRenderer) {
    boardRenderer.draw(camera);
    boardRenderer.drawBench(camera);

    auto drawPokemonList = [&](const std::vector<PokemonInstance>& list) {
        for (auto& instance : list) {
            if (!instance.alive) continue;

            float scaleFactor = instance.model->getScaleFactor();
            glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
            glm::mat4 rotationX = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.x), glm::vec3(1, 0, 0));
            glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.y), glm::vec3(0, 1, 0));
            glm::mat4 rotationZ = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.z), glm::vec3(0, 0, 1));
            glm::mat4 translation = glm::translate(glm::mat4(1.0f), instance.position);

            glm::mat4 modelMat = translation * rotationY * rotationX * rotationZ * scale;
            glm::mat4 mvp = camera.getProjectionMatrix() * camera.getViewMatrix() * modelMat;

            instance.model->drawInstanceWithMVP(mvp);
        }
    };

    drawPokemonList(pokemons);
    drawPokemonList(benchPokemons);
}

std::vector<HealthBarData> GameWorld::getHealthBarData(const Camera3D& camera, int screenWidth, int screenHeight) const {
    std::vector<HealthBarData> data;

    auto process = [&](const PokemonInstance& instance) {
        if (!instance.alive) return;

        glm::vec3 worldPos = instance.position + glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec4 viewport(0.0f, 0.0f, screenWidth, screenHeight);
        glm::vec3 screenPos = glm::project(worldPos, camera.getViewMatrix(), camera.getProjectionMatrix(), viewport);

        if (screenPos.z > 1.0f || screenPos.x < 0 || screenPos.x > screenWidth || screenPos.y < 0 || screenPos.y > screenHeight)
            return;

        HealthBarData hb;
        hb.screenPosition = glm::vec2(screenPos.x, screenHeight - screenPos.y);
        hb.currentHP = instance.hp;
        hb.maxHP = instance.maxHP; // NEW: use scaled per-unit max
        hb.currentEnergy = instance.energy;
        hb.maxEnergy     = instance.maxEnergy;
        data.push_back(hb);
    };

    for (auto& p : pokemons) process(p);
    for (auto& b : benchPokemons) process(b);

    return data;
}

glm::vec3 GameWorld::getNearestEnemyPosition(const PokemonInstance& unit) const {
    float closestDist = std::numeric_limits<float>::max();
    glm::vec3 closestPos = unit.position;

    const auto& list = pokemons;
    for (const auto& other : list) {
        if (!other.alive || other.side == unit.side) continue;
        float d = glm::distance(unit.position, other.position);
        if (d < closestDist) { closestDist = d; closestPos = other.position; }
    }
    return closestPos;
}
