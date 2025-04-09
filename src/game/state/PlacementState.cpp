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

        // Check if the starter is on the board in a valid position
        bool valid = false;
        auto& pokemons = gameWorld->getPokemons();
        auto it = std::find_if(pokemons.begin(), pokemons.end(), [this](const PokemonInstance& p) {
            return p.name == starterName;
        });

        if (it != pokemons.end()) {
            valid = isValidGridPosition(it->position);
        }

        if (!valid) {
            moveStarterToValidGridPosition();
        }

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

bool PlacementState::isValidGridPosition(const glm::vec3& position) const {
    const float cellSize = 1.2f;
    const float epsilon = 0.01f; // Allow slight floating-point imprecision

    // Calculate board origin (center of first cell)
    float boardOriginX = -((8 * cellSize) / 2.0f) + cellSize * 0.5f;
    float boardOriginZ = cellSize * 0.5f;

    // Calculate expected column/row
    int col = static_cast<int>(std::round((position.x - boardOriginX) / cellSize));
    int row = static_cast<int>(std::round((position.z - boardOriginZ) / cellSize));

    // Check if column/row are within valid ranges
    if (col < 0 || col >= 8 || row < 0 || row >= 4) {
        return false;
    }

    // Calculate exact center position for this column/row
    float expectedX = boardOriginX + col * cellSize;
    float expectedZ = boardOriginZ + row * cellSize;

    // Check if position matches the cell center (with epsilon tolerance)
    bool xValid = std::abs(position.x - expectedX) < epsilon;
    bool zValid = std::abs(position.z - expectedZ) < epsilon;

    return xValid && zValid;
}

void PlacementState::moveStarterToValidGridPosition() {
    auto& pokemons = gameWorld->getPokemons();
    auto& bench = gameWorld->getBenchPokemons();

    // Check if the starter is on the bench
    auto benchIt = std::find_if(bench.begin(), bench.end(), [this](const PokemonInstance& p) {
        return p.name == starterName;
    });

    if (benchIt != bench.end()) {
        // Move from bench to board
        PokemonInstance starter = *benchIt;
        bench.erase(benchIt);
        placeOnValidGridPosition(starter);
        pokemons.push_back(starter);
        std::cout << "[PlacementState] Moved starter from bench to valid grid position.\n";
    } else {
        // Check if it's on the board but in an invalid position
        auto boardIt = std::find_if(pokemons.begin(), pokemons.end(), [this](const PokemonInstance& p) {
            return p.name == starterName;
        });

        if (boardIt != pokemons.end()) {
            placeOnValidGridPosition(*boardIt);
            std::cout << "[PlacementState] Adjusted starter position to valid grid cell.\n";
        } else {
            // Not found, add to board (unlikely case)
            PokemonInstance starter;
            starter.name = starterName;
            placeOnValidGridPosition(starter);
            pokemons.push_back(starter);
            std::cout << "[PlacementState] Added missing starter to board.\n";
        }
    }
}

void PlacementState::placeOnValidGridPosition(PokemonInstance& starter) {
    const float cellSize = 1.2f;
    float boardOriginX = -((8 * cellSize) / 2.0f) + cellSize * 0.5f;
    float boardOriginZ = cellSize * 0.5f;

    int col = 3; // Center column (8 columns, index 3 is fourth column)
    int row = 0; // First row
    
    starter.position.x = boardOriginX + col * cellSize;
    starter.position.z = boardOriginZ + row * cellSize;
    starter.position.y = 0.0f;
}