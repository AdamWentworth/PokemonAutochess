// src/game/state/CombatState.cpp
#include "CombatState.h"
#include <iostream>
#include "../GameConfig.h"

CombatState::CombatState(GameStateManager* manager, GameWorld* world, const std::string& scriptPath)
    : stateManager(manager), gameWorld(world), script(world) {
    const auto& cfg = GameConfig::get();
    textRenderer = std::make_unique<TextRenderer>(cfg.fontPath, cfg.fontSize);
    script.loadScript(scriptPath);
    
    movementSystem = std::make_unique<MovementSystem>(gameWorld, gridOccupancy);
}

CombatState::~CombatState() = default;

void CombatState::onEnter() {
    sol::state& lua = script.getState();
    
    sol::function getMessage = lua["getMessage"];
    if (getMessage.valid()) {
        combatMessage = getMessage();
    }

    sol::function getEnemies = lua["getEnemies"];
    if (getEnemies.valid()) {
        const float cellSize = 1.2f;
        const int gridCols = 8;
        const int gridRows = 8;
        
        const float gridWidth = gridCols * cellSize;
        const float gridHeight = gridRows * cellSize;
        const float startX = -gridWidth/2 + cellSize/2;
        const float startZ = -gridHeight/2 + cellSize/2;

        sol::table enemies = getEnemies();
        for (auto& entry : enemies) {
            sol::table enemy = entry.second;
            std::string name = enemy["name"];
            int gridCol = enemy["gridCol"];
            int gridRow = enemy["gridRow"];
            float x = startX + (gridCol * cellSize);
            float z = startZ + (gridRow * cellSize);
            gameWorld->spawnPokemon(name, glm::vec3(x, 0.0f, z), PokemonSide::Enemy);
        }
    }
}

void CombatState::onExit() {}

void CombatState::handleInput(SDL_Event& event) {}

void CombatState::update(float deltaTime) {
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
