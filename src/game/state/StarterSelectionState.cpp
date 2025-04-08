// StarterSelectionState.cpp
#include "StarterSelectionState.h"
#include "../GameStateManager.h"
#include "../GameWorld.h"
#include "../LuaBindings.h"

#include "../../engine/ui/Card.h"
#include "../../engine/ui/UIManager.h"
#include "../../engine/utils/Shader.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

StarterSelectionState::StarterSelectionState(GameStateManager* manager, GameWorld* world)
    : stateManager(manager), gameWorld(world), selectedStarter(StarterPokemon::None)
{
    lua.open_libraries(sol::lib::base);
    registerLuaBindings(lua, world);
    lua.script_file("scripts/states/starter_selection.lua");

    int cardWidth  = 200;
    int cardHeight = 300;
    int spacing    = 50;
    int totalWidth = 3 * cardWidth + 2 * spacing;
    int startX     = (1280 - totalWidth) / 2;
    int startY     = (720 - cardHeight) / 2;

    bulbasaurRect  = { startX,                       startY, cardWidth, cardHeight };
    charmanderRect = { startX + cardWidth + spacing, startY, cardWidth, cardHeight };
    squirtleRect   = { startX + 2 * (cardWidth + spacing), startY, cardWidth, cardHeight };
}

StarterSelectionState::~StarterSelectionState() {
    // No manual cleanup needed
}

void StarterSelectionState::onEnter() {
    std::cout << "[StarterSelectionState] Entering starter selection.\n";
}

void StarterSelectionState::onExit() {
    std::cout << "[StarterSelectionState] Exiting starter selection.\n";
}

void StarterSelectionState::handleInput(SDL_Event& event) {
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        int mouseX = event.button.x;
        int mouseY = event.button.y;

        if (isPointInRect(mouseX, mouseY, bulbasaurRect)) {
            lua["onCardClick"]("bulbasaur");
            stateManager->popState();
        }
        else if (isPointInRect(mouseX, mouseY, charmanderRect)) {
            lua["onCardClick"]("charmander");
            stateManager->popState();
        }
        else if (isPointInRect(mouseX, mouseY, squirtleRect)) {
            lua["onCardClick"]("squirtle");
            stateManager->popState();
        }
    }

    if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_1) {
            lua["onCardClick"]("bulbasaur");
            stateManager->popState();
        }
        if (event.key.keysym.sym == SDLK_2) {
            lua["onCardClick"]("charmander");
            stateManager->popState();
        }
        if (event.key.keysym.sym == SDLK_3) {
            lua["onCardClick"]("squirtle");
            stateManager->popState();
        }
    }
}

void StarterSelectionState::update(float deltaTime) {
    // Add UI animation updates here later
}

void StarterSelectionState::render() {
    UIManager::init();

    glm::mat4 ortho = glm::ortho(0.0f, 1280.0f, 720.0f, 0.0f);
    Shader* uiShader = UIManager::getCardShader();
    uiShader->use();

    GLint projLoc = glGetUniformLocation(uiShader->getID(), "u_Projection");
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(ortho));

    Card bulbasaurCard(bulbasaurRect, "assets/images/bulbasaur.png");
    Card charmanderCard(charmanderRect, "assets/images/charmander.png");
    Card squirtleCard(squirtleRect, "assets/images/squirtle.png");

    bulbasaurCard.draw(uiShader);
    charmanderCard.draw(uiShader);
    squirtleCard.draw(uiShader);

    glUseProgram(0);
}

bool StarterSelectionState::isPointInRect(int x, int y, const SDL_Rect& rect) const {
    return (x >= rect.x && x <= rect.x + rect.w &&
            y >= rect.y && y <= rect.y + rect.h);
}
