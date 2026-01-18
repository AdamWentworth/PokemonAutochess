// src/game/GameWorld.cpp
#include "GameWorld.h"

#include "./engine/utils/ResourceManager.h"
#include "./engine/render/Model.h"
#include "./engine/render/Camera3D.h"
#include "./engine/render/BoardRenderer.h"

#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>

#include "PokemonConfigLoader.h"
#include "GameConfig.h"
#include "MovesConfigLoader.h"

#include <cmath>
#include <limits>
#include <algorithm>

static constexpr int kLoopAnimIndex = 1;

void GameWorld::applyLevelScaling(PokemonInstance& inst, int level) const {
    const auto& cfg = GameConfig::get();
    const int useLevel = (level <= 0) ? cfg.baseLevel : level;

    inst.level = useLevel;

    const float mult = std::pow(1.0f + cfg.perLevelBoost, static_cast<float>(useLevel - 1));

    inst.maxHP         = static_cast<int>(std::round(static_cast<float>(inst.baseHp) * mult));
    inst.hp            = inst.maxHP;
    inst.attack        = static_cast<int>(std::round(static_cast<float>(inst.baseAttack) * mult));
    inst.movementSpeed = inst.baseMovementSpeed * mult;
}

static const LoadoutEntry* pickLoadoutForLevel(const PokemonStats& ps, int level) {
    const LoadoutEntry* best = nullptr;

    // loadoutByLevel is a map<int, LoadoutEntry> sorted by key
    for (const auto& [lvl, le] : ps.loadoutByLevel) {
        if (lvl <= level) best = &le;
        else break;
    }

    return best;
}

void GameWorld::applyLoadoutForLevel(PokemonInstance& inst) const {
    const PokemonStats* ps = PokemonConfigLoader::getInstance().getStats(inst.name);
    if (!ps) {
        inst.fastMove.clear();
        inst.chargedMove.clear();
        inst.maxEnergy = 100;
        inst.energy = 0;
        return;
    }

    const LoadoutEntry* le = pickLoadoutForLevel(*ps, inst.level);
    if (le) {
        inst.fastMove = le->fast;
        inst.chargedMove = le->hasCharged ? le->charged : std::string();
    } else {
        inst.fastMove.clear();
        inst.chargedMove.clear();
    }

    // Energy cap: default 100, but if charged move has an energyCost use that
    inst.maxEnergy = 100;
    if (!inst.chargedMove.empty()) {
        if (const auto* md = MovesConfigLoader::getInstance().getMove(inst.chargedMove)) {
            if (md->energyCost > 0) inst.maxEnergy = md->energyCost;
        }
    }
    inst.energy = 0;
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
    inst.position = startPos;
    inst.model = sharedModel;

    // ✅ FIX: restore expected facing direction
    // Player = 180, Enemy = 0 (matches earlier behavior)
    inst.rotation = glm::vec3(0.0f, (side == PokemonSide::Player ? 180.0f : 0.0f), 0.0f);
    inst.side = side;

    inst.baseHp = stats->hp;
    inst.baseAttack = stats->attack;
    inst.baseMovementSpeed = stats->movementSpeed;

    applyLevelScaling(inst, level);
    applyLoadoutForLevel(inst);

    inst.animTimeSec = 0.0f;

    pokemons.push_back(inst);

    std::cout << "[GameWorld] Spawned " << pokemonName
              << " (ID: " << inst.id
              << ", L" << inst.level
              << ", HP: " << inst.hp << "/" << inst.maxHP
              << ", ATK: " << inst.attack
              << ", SPD: " << inst.movementSpeed
              << ", FAST: " << (inst.fastMove.empty() ? "-" : inst.fastMove)
              << ", CHARGED: " << (inst.chargedMove.empty() ? "-" : inst.chargedMove)
              << ", Ecap: " << inst.maxEnergy
              << ")\n";
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

    // Bench stays facing same as before
    inst.rotation = glm::vec3(0.0f, 180.0f, 0.0f);
    inst.side = PokemonSide::Player;

    inst.baseHp = stats->hp;
    inst.baseAttack = stats->attack;
    inst.baseMovementSpeed = stats->movementSpeed;

    applyLevelScaling(inst, -1);
    applyLoadoutForLevel(inst);

    int slot = static_cast<int>(benchPokemons.size());
    float spacing = 1.2f;
    float x = (slot - 4) * spacing + spacing / 2.0f;
    float z = 4.5f;
    inst.position = glm::vec3(x, 0.0f, z);

    inst.animTimeSec = 0.0f;

    benchPokemons.push_back(inst);

    std::cout << "[GameWorld] Benched " << pokemonName
              << " (ID: " << inst.id
              << " L" << inst.level
              << ", FAST: " << (inst.fastMove.empty() ? "-" : inst.fastMove)
              << ", CHARGED: " << (inst.chargedMove.empty() ? "-" : inst.chargedMove)
              << ")\n";
}

const PokemonInstance* GameWorld::getPokemonByName(const std::string& name) const {
    for (const auto& p : pokemons) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

std::vector<PokemonInstance>& GameWorld::getPokemons() { return pokemons; }
std::vector<PokemonInstance>& GameWorld::getBenchPokemons() { return benchPokemons; }

void GameWorld::update(float dt)
{
    auto tickList = [&](std::vector<PokemonInstance>& list) {
        for (auto& p : list) {
            if (!p.alive || !p.model) continue;

            float dur = p.model->getAnimationDurationSec(kLoopAnimIndex);
            p.animTimeSec += dt;

            if (dur > 0.0f && p.animTimeSec > dur) {
                p.animTimeSec = std::fmod(p.animTimeSec, dur);
            }
        }
    };

    tickList(pokemons);
    tickList(benchPokemons);

    // tail fire particle update
    charmanderTailFireVfx.update(dt, pokemons, benchPokemons);
}

void GameWorld::drawAll(const Camera3D& camera, BoardRenderer& boardRenderer) {
    boardRenderer.draw(camera);
    boardRenderer.drawBench(camera);

    auto drawPokemonList = [&](const std::vector<PokemonInstance>& list) {
        for (const auto& instance : list) {
            if (!instance.alive || !instance.model) continue;

            float scaleFactor = instance.model->getScaleFactor();

            glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
            glm::mat4 rotationX = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.x), glm::vec3(1, 0, 0));
            glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.y), glm::vec3(0, 1, 0));
            glm::mat4 rotationZ = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.z), glm::vec3(0, 0, 1));
            glm::mat4 translation = glm::translate(glm::mat4(1.0f), instance.position);

            glm::mat4 instanceTransform = translation * rotationY * rotationX * rotationZ * scale;

            instance.model->drawAnimated(camera, instanceTransform, instance.animTimeSec, kLoopAnimIndex);
        }
    };

    drawPokemonList(pokemons);
    drawPokemonList(benchPokemons);

    // draw particles AFTER opaque models
    charmanderTailFireVfx.render(camera);
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
        hb.maxHP = instance.maxHP;
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

    for (const auto& other : pokemons) {
        if (!other.alive || other.side == unit.side) continue;
        float d = glm::distance(unit.position, other.position);
        if (d < closestDist) {
            closestDist = d;
            closestPos = other.position;
        }
    }

    return closestPos;
}
