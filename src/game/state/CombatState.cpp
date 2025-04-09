// CombatState.cpp

#include "CombatState.h"
#include "../GameStateManager.h"
#include "../GameWorld.h"
#include "../../engine/ui/TextRenderer.h"
#include <iostream>

CombatState::CombatState(GameStateManager* manager, GameWorld* world)
    : stateManager(manager), gameWorld(world),
      textRenderer(std::make_unique<TextRenderer>("assets/fonts/GillSans.ttf", 48)) {}

void CombatState::onEnter() {
    std::cout << "[CombatState] Entering combat.\n";
    
    // Spawn Pidgey on enemy side (opposite end of board)
    const float enemyZ = -4.5f;  // Negative Z for enemy side
    gameWorld->spawnPokemon("pidgey", glm::vec3(0.0f, 0.0f, enemyZ), PokemonSide::Enemy);
}

void CombatState::onExit() {
    std::cout << "[CombatState] Exiting combat.\n";
}

void CombatState::handleInput(SDL_Event& event) {
    // Handle input if needed
}

void CombatState::update(float deltaTime) {
    // Update logic if needed
}

void CombatState::render() {
    const std::string message = "Route 1";
    const float scale = 1.0f;
    const int windowWidth = 1280;

    float textWidth = textRenderer->measureTextWidth(message, scale);
    float centeredX = std::round((windowWidth - textWidth) / 2.0f);
    textRenderer->renderText(message, centeredX, 50.0f, glm::vec3(1.0f), scale);
}

CombatState::~CombatState() {
}