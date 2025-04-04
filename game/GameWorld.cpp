// GameWorld.cpp

#include "GameWorld.h"
#include "../engine/core/ResourceManager.h"
#include "../engine/render/Model.h"
#include "../engine/render/Camera3D.h"
#include <iostream>

void GameWorld::spawnPokemon(const std::string& pokemonName, const glm::vec3& startPos) {
    // Build a path to the model file, or switch logic if you want each name => path
    // For example: "assets/models/" + pokemonName + ".glb"
    std::string path = "assets/models/" + pokemonName + ".glb";

    // Load or get from cache
    Model* sharedModel = ResourceManager::getInstance().getModel(path);

    // Create an instance
    PokemonInstance inst;
    inst.model = sharedModel;
    inst.position = startPos;

    // Add it to the vector
    pokemons.push_back(inst);

    std::cout << "[GameWorld] Spawned " << pokemonName << " at ("
              << startPos.x << ", " << startPos.y << ", " << startPos.z << ")\n";
}

void GameWorld::drawAll(const Camera3D& camera) {
    // For each Pokémon, set its position on the model,
    // then call model->draw().
    // NOTE: This works if each Model can store position individually – 
    // but in your code, Model has a single position field, so you
    // must set it before each draw, then draw, then set it back, etc.
    for (auto& poke : pokemons) {
        // This will mutate the Model's internal position
        // for that *shared* Model. If you share the same Model 
        // among multiple instances, you need a different approach 
        // (like a "ModelInstance" system). But for a prototype, this is OK.
        poke.model->setModelPosition(poke.position);
        poke.model->draw(camera);
    }
}
