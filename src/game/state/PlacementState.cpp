// PlacementState.cpp

#include "PlacementState.h"
#include "Route1State.h"
#include "../GameStateManager.h"
#include "../GameWorld.h"
#include "../../engine/ui/TextRenderer.h"
#include <iostream>
#include <algorithm>

static TextRenderer* textRenderer = nullptr;

PlacementState::PlacementState(GameStateManager* manager, GameWorld* world, const std::string& starterName)
    : stateManager(manager),
      gameWorld(world),
      starterName(starterName),
      timer(5.0f),
      placementDone(false)
{
    if (!textRenderer) {
        textRenderer = new TextRenderer("assets/fonts/GillSans.ttf", 48);
    }
}

PlacementState::~PlacementState() {}

void PlacementState::onEnter() {
    std::cout << "[PlacementState] Entering placement phase. Place your starter within 5 seconds.\n";
}

void PlacementState::onExit() {
    std::cout << "[PlacementState] Exiting placement phase.\n";
}

void PlacementState::handleInput(SDL_Event& event) {
    // Optionally handle input for early confirmation
}

void PlacementState::update(float deltaTime) {
    timer -= deltaTime;

    if (timer <= 0.0f && !placementDone) {
        placementDone = true;
        if (!isStarterOnBoard()) {
            moveStarterToBoard();
        }
        // Transition to combat state instead of quitting
    stateManager->pushState(std::make_unique<Route1State>(stateManager, gameWorld));
    }
}

void PlacementState::render() {
    const std::string message = "Place your starter! Time left: " + std::to_string(static_cast<int>(timer));
    const float scale = 1.0f;
    const int windowWidth = 1280;

    float textWidth = textRenderer->measureTextWidth(message, scale);
    float centeredX = std::round((windowWidth - textWidth) / 2.0f);
    float textY = 50.0f;

    textRenderer->renderText(message, centeredX, textY, glm::vec3(1.0f), scale);
}

bool PlacementState::isStarterOnBoard() const {
    const auto& pokemons = gameWorld->getPokemons();
    return std::any_of(pokemons.begin(), pokemons.end(), [this](const PokemonInstance& p) {
        return p.name == starterName;
    });
}

void PlacementState::moveStarterToBoard() {
    auto& bench = gameWorld->getBenchPokemons();
    auto it = std::find_if(bench.begin(), bench.end(), [this](const PokemonInstance& p) {
        return p.name == starterName;
    });

    if (it != bench.end()) {
        PokemonInstance starter = *it;
        bench.erase(it);

        // Place in the center of the first row
        float cellSize = 1.2f;
        float boardOriginX = -((8 * cellSize) / 2.0f) + cellSize * 0.5f;
        float boardOriginZ = cellSize * 0.5f;
        int col = 3; // Center column (8 columns, index 3 is fourth column)
        
        starter.position.x = boardOriginX + col * cellSize;
        starter.position.z = boardOriginZ;
        starter.position.y = 0.0f;

        gameWorld->getPokemons().push_back(starter);
        std::cout << "[PlacementState] Moved starter to board at (" << starter.position.x << ", " << starter.position.z << ")\n";
    }
}