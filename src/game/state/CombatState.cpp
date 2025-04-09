// src/game/state/CombatState.cpp
#include "CombatState.h"
#include <iostream>

CombatState::CombatState(GameStateManager* manager, GameWorld* world, const std::string& scriptPath)
    : stateManager(manager), gameWorld(world), script(world) {
    textRenderer = std::make_unique<TextRenderer>("assets/fonts/GillSans.ttf", 48);
    script.loadScript(scriptPath);
    
    // Initialize MovementSystem with gameWorld and gridOccupancy
    movementSystem = std::make_unique<MovementSystem>(gameWorld, gridOccupancy);
}

CombatState::~CombatState() = default;

void CombatState::onEnter() {
    sol::state& lua = script.getState();
    
    // Load combat message
    sol::function getMessage = lua["getMessage"];
    if (getMessage.valid()) {
        combatMessage = getMessage();
    }

    // Load enemies
    sol::function getEnemies = lua["getEnemies"];
    if (getEnemies.valid()) {
        const float cellSize = 1.2f;
        const int gridCols = 8;
        const int gridRows = 8;
        
        // Grid calculations
        const float gridWidth = gridCols * cellSize;
        const float gridHeight = gridRows * cellSize;
        const float startX = -gridWidth/2 + cellSize/2;  // First column center X
        const float startZ = -gridHeight/2 + cellSize/2; // First row center Z

        sol::table enemies = getEnemies();
        for (auto& entry : enemies) {
            sol::table enemy = entry.second;
            std::string name = enemy["name"];
            
            // Get grid position
            int gridCol = enemy["gridCol"];
            int gridRow = enemy["gridRow"];
            
            // Convert to world position
            float x = startX + (gridCol * cellSize);
            float z = startZ + (gridRow * cellSize);
            
            // Spawn facing player (0° Y rotation)
            gameWorld->spawnPokemon(
                name, 
                glm::vec3(x, 0.0f, z), 
                PokemonSide::Enemy
            );
        }
    }
}

void CombatState::onExit() {
    // Cleanup if needed
}

void CombatState::handleInput(SDL_Event& event) {
    // Handle input if necessary
}

void CombatState::update(float deltaTime) {
    // Update the MovementSystem during combat
    if (movementSystem) {
        movementSystem->update(deltaTime);
    }
}

void CombatState::render() {
    const float scale = 1.0f;
    const int windowWidth = 1280;
    
    float textWidth = textRenderer->measureTextWidth(combatMessage, scale);
    float centeredX = std::round((windowWidth - textWidth) / 2.0f);
    textRenderer->renderText(combatMessage, centeredX, 50.0f, glm::vec3(1.0f), scale);
}