// GameWorld.cpp
#include "GameWorld.h"
#include "../engine/utils/ResourceManager.h"
#include "../engine/render/Model.h"
#include "../engine/render/Camera3D.h"
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h> // Ensure OpenGL functions and symbols are defined

void GameWorld::spawnPokemon(const std::string& pokemonName, const glm::vec3& startPos, PokemonSide side) {
    // Build the file path (e.g., "assets/models/bulbasaur.glb")
    std::string path = "assets/models/" + pokemonName + ".glb";
    
    // Get the shared model (or load it) from ResourceManager.
    Model* sharedModel = ResourceManager::getInstance().getModel(path);

    // Create a new instance with per-instance data.
    PokemonInstance inst;
    inst.model = sharedModel;
    inst.position = startPos;
    inst.rotation = glm::vec3(90.0f, (side == PokemonSide::Player ? 180.0f : 0.0f), 0.0f);
    inst.side = side;

    pokemons.push_back(inst);

    std::cout << "[GameWorld] Spawned " << pokemonName << " at (" 
              << startPos.x << ", " << startPos.y << ", " << startPos.z << ") on side " 
              << (side == PokemonSide::Player ? "Player" : "Enemy") << "\n";
}

void GameWorld::drawAll(const Camera3D& camera) {
    // For each Pokémon instance, compute the model matrix and then the MVP.
    for (auto& instance : pokemons) {
        float scaleFactor = instance.model->getScaleFactor();
        glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor));
        glm::mat4 rotationX = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.x), glm::vec3(1, 0, 0));
        glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.y), glm::vec3(0, 1, 0));
        glm::mat4 rotationZ = glm::rotate(glm::mat4(1.0f), glm::radians(instance.rotation.z), glm::vec3(0, 0, 1));
        glm::mat4 translation = glm::translate(glm::mat4(1.0f), instance.position);

        // Compose the model matrix.
        glm::mat4 modelMat = translation * rotationY * rotationX * rotationZ * scale;

        // Build the MVP (Model-View-Projection) matrix.
        glm::mat4 mvp = camera.getProjectionMatrix() * camera.getViewMatrix() * modelMat;

        // Use the new method to set the MVP and draw.
        instance.model->drawInstanceWithMVP(mvp);
    }
}
