// StarterSelectionState.cpp
#include "StarterSelectionState.h"
#include "GameStateManager.h"  // Needed for popState()
#include "GameWorld.h"
#include "../engine/utils/Shader.h"  // Our custom Shader class
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

// Include the Card component header.
#include "../engine/ui/Card.h"

// Helper function to get the UI shader as a Shader object.
Shader* getUIShader() {
    static Shader* uiShader = nullptr;
    if (!uiShader) {
        uiShader = new Shader("assets/shaders/ui/card.vert", "assets/shaders/ui/card.frag");
    }
    return uiShader;
}

StarterSelectionState::StarterSelectionState(GameStateManager* manager, GameWorld* world)
    : stateManager(manager), gameWorld(world), selectedStarter(StarterPokemon::None)
{
    int cardWidth  = 200;
    int cardHeight = 300;
    int spacing    = 50;
    int totalWidth = 3 * cardWidth + 2 * spacing;
    int startX     = (1280 - totalWidth) / 2;
    int startY     = (720 - cardHeight) / 2;

    // Create card components.
    // We store these as member variables if needed; for simplicity, we'll create them on the fly here.
    // Alternatively, you could add:
    //   std::vector<Card> cards;
    // in your StarterSelectionState class.
    bulbasaurRect  = { startX,                       startY, cardWidth, cardHeight };
    charmanderRect = { startX + cardWidth + spacing, startY, cardWidth, cardHeight };
    squirtleRect   = { startX + 2 * (cardWidth + spacing), startY, cardWidth, cardHeight };
}

StarterSelectionState::~StarterSelectionState() {
    // If getUIShader() allocated a shader, you might delete it on state exit if it’s not used elsewhere.
}

void StarterSelectionState::onEnter() {
    std::cout << "[StarterSelectionState] Entering starter selection.\n";
}

void StarterSelectionState::onExit() {
    std::cout << "[StarterSelectionState] Exiting starter selection.\n";
}

void StarterSelectionState::handleInput(SDL_Event& event) {
    // (Input handling remains the same.)
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        int mouseX = event.button.x;
        int mouseY = event.button.y;
        glm::vec3 ourSideSpawnPos(0.0f, 0.0f, 3.5f);

        // Instead of checking against SDL_Rects directly, you could instantiate Card objects and call isPointInside().
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

    // Keyboard-based selection remains unchanged.
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
    // UI updates or animations if needed.
}

void StarterSelectionState::render() {
    // Set up an orthographic projection for UI rendering.
    glm::mat4 ortho = glm::ortho(0.0f, 1280.0f, 720.0f, 0.0f);
    Shader* uiShader = getUIShader();
    uiShader->use();

    GLint projLoc = glGetUniformLocation(uiShader->getID(), "u_Projection");
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(ortho));

    // Instead of calling drawCard() directly, instantiate Card components and call their draw() method.
    Card bulbasaurCard(bulbasaurRect, glm::vec3(0.2f, 0.7f, 0.2f));
    Card charmanderCard(charmanderRect, glm::vec3(0.7f, 0.2f, 0.2f));
    Card squirtleCard(squirtleRect, glm::vec3(0.2f, 0.2f, 0.7f));

    bulbasaurCard.draw(uiShader);
    charmanderCard.draw(uiShader);
    squirtleCard.draw(uiShader);

    glUseProgram(0);
}
