// src/game/state/CombatState.cpp
#include "CombatState.h"
#include <iostream>

CombatState::CombatState(GameStateManager* manager, GameWorld* world, const std::string& scriptPath)
    : stateManager(manager), gameWorld(world), script(world) {
    textRenderer = std::make_unique<TextRenderer>("assets/fonts/GillSans.ttf", 48);
    script.loadScript(scriptPath);
}

CombatState::~CombatState() = default;

void CombatState::onEnter() {
    sol::state& lua = script.getState();
    
    // Load combat message from Lua
    sol::function getMessage = lua["getMessage"];
    if (getMessage.valid()) {
        combatMessage = getMessage();
    } else {
        combatMessage = "Default Combat Message";
    }

    // Load enemies from Lua
    sol::function getEnemies = lua["getEnemies"];
    if (getEnemies.valid()) {
        sol::table enemies = getEnemies();
        for (auto& entry : enemies) {
            sol::table enemy = entry.second;
            std::string name = enemy["name"];
            float x = enemy["x"];
            float y = enemy["y"];
            float z = enemy["z"];
            gameWorld->spawnPokemon(name, glm::vec3(x, y, z), PokemonSide::Enemy);
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
    // Update logic if needed
}

void CombatState::render() {
    const float scale = 1.0f;
    const int windowWidth = 1280;
    
    float textWidth = textRenderer->measureTextWidth(combatMessage, scale);
    float centeredX = std::round((windowWidth - textWidth) / 2.0f);
    textRenderer->renderText(combatMessage, centeredX, 50.0f, glm::vec3(1.0f), scale);
}