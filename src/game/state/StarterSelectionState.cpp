// StarterSelectionState.cpp
#include "StarterSelectionState.h"
#include "../GameStateManager.h"
#include "../GameWorld.h"

#include "../../engine/ui/Card.h"
#include "../../engine/ui/UIManager.h"
#include "../../engine/utils/Shader.h"
#include "../ui/CardFactory.h"

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

    cardSystem.init();

    std::vector<CardData> starters = {
        { "bulbasaur", 0, CardType::Starter },
        { "charmander", 0, CardType::Starter },
        { "squirtle", 0, CardType::Starter }
    };

    auto starterCards = CardFactory::createCardRow(starters, 1280, 300);
    for (auto& card : starterCards) {
        cardSystem.addCard(std::move(card));
    }
}

StarterSelectionState::~StarterSelectionState() {}

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

        auto clicked = cardSystem.handleMouseClick(mouseX, mouseY);
        if (clicked.has_value()) {
            const std::string& name = clicked->pokemonName;
            script.call("onCardClick", name);
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
    cardSystem.render(1280, 720);
}

bool StarterSelectionState::isPointInRect(int x, int y, const SDL_Rect& rect) const {
    return (x >= rect.x && x <= rect.x + rect.w &&
            y >= rect.y && y <= rect.y + rect.h);
}
