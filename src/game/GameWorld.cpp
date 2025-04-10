// GameWorld.cpp
#include "GameWorld.h"
#include "../engine/utils/ResourceManager.h"
#include "../engine/render/Model.h"
#include "../engine/render/Camera3D.h"
#include "../engine/render/BoardRenderer.h" // NEW include
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include "PokemonConfigLoader.h"

void GameWorld::spawnPokemon(const std::string& pokemonName, const glm::vec3& startPos, PokemonSide side) {
    const PokemonStats* stats = PokemonConfigLoader::getInstance().getStats(pokemonName);
    if (!stats) {
        std::cerr << "[GameWorld] No config found for Pokémon: " << pokemonName << "\n";
        return;
    }

    std::string path = "assets/models/" + stats->model;
    auto sharedModel = ResourceManager::getInstance().getModel(path);

    // Then assign it to the Pokémon instance:
    PokemonInstance inst;
    inst.name = pokemonName;
    inst.model = sharedModel;
    inst.position = startPos;
    inst.rotation = glm::vec3(90.0f, (side == PokemonSide::Player ? 180.0f : 0.0f), 0.0f);
    inst.side = side;

    inst.hp = stats->hp;
    inst.attack = stats->attack;
    inst.movementSpeed = stats->movementSpeed;

    pokemons.push_back(inst);

    std::cout << "[GameWorld] Spawned " << pokemonName
              << " (HP: " << inst.hp << ", ATK: " << inst.attack << ")\n";
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
    inst.name = pokemonName;
    inst.model = sharedModel;
    inst.rotation = glm::vec3(90.0f, 180.0f, 0.0f);
    inst.side = PokemonSide::Player;

    inst.hp = stats->hp;
    inst.attack = stats->attack;
    inst.movementSpeed = stats->movementSpeed;

    int slot = static_cast<int>(benchPokemons.size());
    float spacing = 1.2f;
    float x = (slot - 4) * spacing + spacing / 2.0f;
    float z = 4.5f; // In front of the board
    inst.position = glm::vec3(x, 0.0f, z);

    benchPokemons.push_back(inst);

    std::cout << "[GameWorld] Benched " << pokemonName
              << " at slot " << slot << "\n";
}

const PokemonInstance* GameWorld::getPokemonByName(const std::string& name) const {
    for (const auto& p : pokemons) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

std::vector<PokemonInstance>& GameWorld::getPokemons() {
    return pokemons;
}

std::vector<PokemonInstance>& GameWorld::getBenchPokemons() {
    return benchPokemons;
}

void GameWorld::drawAll(const Camera3D& camera, BoardRenderer& boardRenderer) {
    // Draw main grid and bench
    boardRenderer.draw(camera);
    boardRenderer.drawBench(camera);

    auto drawPokemonList = [&](const std::vector<PokemonInstance>& list) {
        for (auto& instance : list) {
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
        const PokemonStats* stats = PokemonConfigLoader::getInstance().getStats(instance.name);
        if (!stats) return;

        glm::vec3 worldPos = instance.position + glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec4 viewport(0.0f, 0.0f, screenWidth, screenHeight);
        glm::vec3 screenPos = glm::project(worldPos, camera.getViewMatrix(), camera.getProjectionMatrix(), viewport);
        
        if (screenPos.z > 1.0f || screenPos.x < 0 || screenPos.x > screenWidth || screenPos.y < 0 || screenPos.y > screenHeight) 
            return;

        HealthBarData hb;
        hb.screenPosition = glm::vec2(screenPos.x, screenHeight - screenPos.y);
        hb.currentHP = instance.hp;
        hb.maxHP = stats->hp;
        data.push_back(hb);
    };

    for (auto& p : pokemons) process(p);
    for (auto& b : benchPokemons) process(b);
    
    return data;
}
