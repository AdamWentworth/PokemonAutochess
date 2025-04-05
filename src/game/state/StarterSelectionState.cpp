// StarterSelectionState.cpp
#include "StarterSelectionState.h"
#include "GameStateManager.h"
#include "GameWorld.h"
#include "../engine/ui/Card.h"
#include "../engine/utils/Shader.h"
#include "../engine/ui/UIManager.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

StarterSelectionState::StarterSelectionState(GameStateManager* manager, GameWorld* world)
    : stateManager(manager), gameWorld(world), selectedStarter(StarterPokemon::None)
{
    int cardWidth  = 200;
    int cardHeight = 300;
    int spacing    = 50;
    int totalWidth = 3 * cardWidth + 2 * spacing;
    int startX     = (1280 - totalWidth) / 2;
    int startY     = (720 - cardHeight) / 2;

    // Define card rectangles.
    bulbasaurRect  = { startX,                       startY, cardWidth, cardHeight };
    charmanderRect = { startX + cardWidth + spacing, startY, cardWidth, cardHeight };
    squirtleRect   = { startX + 2 * (cardWidth + spacing), startY, cardWidth, cardHeight };
}

StarterSelectionState::~StarterSelectionState() {
    // UIManager will handle its shader cleanup.
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
        glm::vec3 ourSideSpawnPos(0.0f, 0.0f, 3.5f);

        if (isPointInRect(mouseX, mouseY, bulbasaurRect)) {
            selectedStarter = StarterPokemon::Bulbasaur;
            std::cout << "Bulbasaur selected\n";
            gameWorld->spawnPokemon("bulbasaur", ourSideSpawnPos);
            stateManager->popState();
        }
        else if (isPointInRect(mouseX, mouseY, charmanderRect)) {
            selectedStarter = StarterPokemon::Charmander;
            std::cout << "Charmander selected\n";
            gameWorld->spawnPokemon("charmander", ourSideSpawnPos);
            stateManager->popState();
        }
        else if (isPointInRect(mouseX, mouseY, squirtleRect)) {
            selectedStarter = StarterPokemon::Squirtle;
            std::cout << "Squirtle selected\n";
            gameWorld->spawnPokemon("squirtle", ourSideSpawnPos);
            stateManager->popState();
        }
    }

    if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_1) {
            gameWorld->spawnPokemon("bulbasaur", glm::vec3(0.0f));
            stateManager->popState();
        }
        if (event.key.keysym.sym == SDLK_2) {
            gameWorld->spawnPokemon("charmander", glm::vec3(0.0f));
            stateManager->popState();
        }
        if (event.key.keysym.sym == SDLK_3) {
            std::cout << "No squirtle model yet!\n";
        }
    }
}

void StarterSelectionState::update(float deltaTime) {
    // UI animations or updates can be handled here if needed.
}

void StarterSelectionState::render() {
    // Ensure UIManager is initialized.
    UIManager::init();

    // Set up an orthographic projection.
    glm::mat4 ortho = glm::ortho(0.0f, 1280.0f, 720.0f, 0.0f);
    Shader* uiShader = UIManager::getCardShader();
    uiShader->use();

    GLint projLoc = glGetUniformLocation(uiShader->getID(), "u_Projection");
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(ortho));

    // Create Card components.
    Card bulbasaurCard(bulbasaurRect, glm::vec3(0.2f, 0.7f, 0.2f));
    Card charmanderCard(charmanderRect, glm::vec3(0.7f, 0.2f, 0.2f));
    Card squirtleCard(squirtleRect, glm::vec3(0.2f, 0.2f, 0.7f));

    // Draw the cards.
    bulbasaurCard.draw(uiShader);
    charmanderCard.draw(uiShader);
    squirtleCard.draw(uiShader);

    glUseProgram(0);
}
