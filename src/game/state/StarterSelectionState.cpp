// StarterSelectionState.cpp
#include "StarterSelectionState.h"
#include "../GameStateManager.h"
#include "../GameWorld.h"

#include "../../engine/ui/Card.h"
#include "../../engine/ui/UIManager.h"
#include "../../engine/utils/Shader.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

StarterSelectionState::StarterSelectionState(GameStateManager* manager, GameWorld* world)
    : stateManager(manager),
      gameWorld(world),
      selectedStarter(StarterPokemon::None),
      script(world)
{
    script.loadScript("scripts/states/starter_selection.lua");

    // We define card geometry so we know where each card is placed on the screen.
    int cardWidth  = 220;
    int cardHeight = 150;
    int spacing    = 50;
    int totalWidth = 3 * cardWidth + 2 * spacing;
    int startX     = (1280 - totalWidth) / 2;
    int startY     = (720 - cardHeight) / 2;

    bulbasaurRect  = { startX,                       startY, cardWidth, cardHeight };
    charmanderRect = { startX + cardWidth + spacing, startY, cardWidth, cardHeight };
    squirtleRect   = { startX + 2 * (cardWidth + spacing), startY, cardWidth, cardHeight };

    // Initialize the CardSystem (which will internally init UIManager too)
    cardSystem.init();

    // Create actual Card objects with positions & images.
    Card bulbasaurCard(bulbasaurRect,  "assets/images/bulbasaur.png");
    Card charmanderCard(charmanderRect, "assets/images/charmander.png");
    Card squirtleCard(squirtleRect,      "assets/images/squirtle.png");

    // Add them to the system by moving them into the CardSystem.
    cardSystem.addCard(std::move(bulbasaurCard));
    cardSystem.addCard(std::move(charmanderCard));
    cardSystem.addCard(std::move(squirtleCard));
}

StarterSelectionState::~StarterSelectionState() {
    // Nothing special needed here. The cardSystem doesn’t own the shader
    // so we don’t want to call UIManager::shutdown() too early.
}

void StarterSelectionState::onEnter() {
    std::cout << "[StarterSelectionState] Entering starter selection.\n";
    script.onEnter();
}

void StarterSelectionState::onExit() {
    std::cout << "[StarterSelectionState] Exiting starter selection.\n";
    script.onExit();
}

void StarterSelectionState::handleInput(SDL_Event& event) {
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        int mouseX = event.button.x;
        int mouseY = event.button.y;

        // Old direct checks remain (if needed)
        if (isPointInRect(mouseX, mouseY, bulbasaurRect)) {
            script.call("onCardClick", "bulbasaur");
            stateManager->popState();
        }
        else if (isPointInRect(mouseX, mouseY, charmanderRect)) {
            script.call("onCardClick", "charmander");
            stateManager->popState();
        }
        else if (isPointInRect(mouseX, mouseY, squirtleRect)) {
            script.call("onCardClick", "squirtle");
            stateManager->popState();
        }
    }

    if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_1) {
            script.call("onCardClick", "bulbasaur");
            stateManager->popState();
        }
        if (event.key.keysym.sym == SDLK_2) {
            script.call("onCardClick", "charmander");
            stateManager->popState();
        }
        if (event.key.keysym.sym == SDLK_3) {
            script.call("onCardClick", "squirtle");
            stateManager->popState();
        }
    }
}

void StarterSelectionState::update(float deltaTime) {
    script.onUpdate(deltaTime);
    cardSystem.update(deltaTime);
}

void StarterSelectionState::render() {
    // Render through the CardSystem.
    cardSystem.render(1280, 720);
}

bool StarterSelectionState::isPointInRect(int x, int y, const SDL_Rect& rect) const {
    return (x >= rect.x && x <= rect.x + rect.w &&
            y >= rect.y && y <= rect.y + rect.h);
}
