// PlacementState.cpp

#include "PlacementState.h"
// CHANGED: use CombatState and flow.lua instead of hardcoded Route1
#include "CombatState.h"
#include "../GameStateManager.h"
#include "../GameWorld.h"
#include "../../engine/ui/TextRenderer.h"
#include "../GameConfig.h"
#include <iostream>
#include <algorithm>
#include <sol/sol.hpp>

static TextRenderer* textRenderer = nullptr;

PlacementState::PlacementState(GameStateManager* manager, GameWorld* world, const std::string& starterName)
    : stateManager(manager),
      gameWorld(world),
      starterName(starterName),
      timer(5.0f),
      placementDone(false)
{
    if (!textRenderer) {
        const auto& cfg = GameConfig::get();
        textRenderer = new TextRenderer(cfg.fontPath, cfg.fontSize);
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
    (void)event;
}

void PlacementState::update(float deltaTime) {
    timer -= deltaTime;

    if (timer <= 0.0f && !placementDone) {
        placementDone = true;

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

        // NEW: ask Lua which combat script to use next
        static std::unique_ptr<LuaScript> flow;
        if (!flow) {
            flow = std::make_unique<LuaScript>(gameWorld);
            flow->loadScript("scripts/states/flow.lua");
        }
        std::string routeScript = "scripts/states/route1.lua";
        sol::state& L = flow->getState();
        sol::function next_route = L["next_route_after_placement"];
        if (next_route.valid()) {
            sol::object r = next_route(starterName);
            if (r.is<std::string>()) routeScript = r.as<std::string>();
        }

        stateManager->pushState(std::make_unique<CombatState>(stateManager, gameWorld, routeScript));
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

        float cellSize = 1.2f;
        float boardOriginX = -((8 * cellSize) / 2.0f) + cellSize * 0.5f;
        float boardOriginZ = cellSize * 0.5f;
        int col = 3;
        
        starter.position.x = boardOriginX + col * cellSize;
        starter.position.z = boardOriginZ;
        starter.position.y = 0.0f;

        gameWorld->getPokemons().push_back(starter);
        std::cout << "[PlacementState] Moved starter to board at (" << starter.position.x << ", " << starter.position.z << ")\n";
    }
}

bool PlacementState::isValidGridPosition(const glm::vec3& position) const {
    const float cellSize = 1.2f;
    const float epsilon = 0.01f;

    float boardOriginX = -((8 * cellSize) / 2.0f) + cellSize * 0.5f;
    float boardOriginZ = cellSize * 0.5f;

    int col = static_cast<int>(std::round((position.x - boardOriginX) / cellSize));
    int row = static_cast<int>(std::round((position.z - boardOriginZ) / cellSize));

    if (col < 0 || col >= 8 || row < 0 || row >= 4) {
        return false;
    }

    float expectedX = boardOriginX + col * cellSize;
    float expectedZ = boardOriginZ + row * cellSize;

    bool xValid = std::abs(position.x - expectedX) < epsilon;
    bool zValid = std::abs(position.z - expectedZ) < epsilon;

    return xValid && zValid;
}

void PlacementState::moveStarterToValidGridPosition() {
    auto& pokemons = gameWorld->getPokemons();
    auto& bench = gameWorld->getBenchPokemons();

    auto benchIt = std::find_if(bench.begin(), bench.end(), [this](const PokemonInstance& p) {
        return p.name == starterName;
    });

    if (benchIt != bench.end()) {
        PokemonInstance starter = *benchIt;
        bench.erase(benchIt);
        placeOnValidGridPosition(starter);
        pokemons.push_back(starter);
        std::cout << "[PlacementState] Moved starter from bench to valid grid position.\n";
    } else {
        auto boardIt = std::find_if(pokemons.begin(), pokemons.end(), [this](const PokemonInstance& p) {
            return p.name == starterName;
        });

        if (boardIt != pokemons.end()) {
            placeOnValidGridPosition(*boardIt);
            std::cout << "[PlacementState] Adjusted starter position to valid grid cell.\n";
        } else {
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

    int col = 3;
    int row = 0;
    
    starter.position.x = boardOriginX + col * cellSize;
    starter.position.z = boardOriginZ + row * cellSize;
    starter.position.y = 0.0f;
}
