// PlacementState.cpp

#include "PlacementState.h"
#include "CombatState.h"
#include "../GameStateManager.h"
#include "../GameWorld.h"
#include "../PokemonInstance.h"          // ← needed
#include "../LuaScript.h"                // ← needed
#include "../../engine/ui/TextRenderer.h"
#include "../GameConfig.h"

#include <sol/sol.hpp>
#include <iostream>
#include <algorithm>
#include <cmath>                         // ← std::round, std::abs

static TextRenderer* textRenderer = nullptr;

static glm::vec3 gridToWorld(int col, int row) {
    const auto& cfg = GameConfig::get();
    float boardOriginX = -((cfg.cols * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    float boardOriginZ = -((cfg.rows * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    return { boardOriginX + col * cfg.cellSize, 0.0f, boardOriginZ + row * cfg.cellSize };
}
static glm::ivec2 worldToGrid(const glm::vec3& pos) {
    const auto& cfg = GameConfig::get();
    float boardOriginX = -((cfg.cols * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    float boardOriginZ = -((cfg.rows * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    int col = static_cast<int>(std::round((pos.x - boardOriginX) / cfg.cellSize));
    int row = static_cast<int>(std::round((pos.z - boardOriginZ) / cfg.cellSize));
    return { col, row };
}

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

PlacementState::~PlacementState() {
    // If you want TextRenderer to live across instances, keep it.
    // If not, uncomment next lines to free it when this state is destroyed.
    // if (textRenderer) { delete textRenderer; textRenderer = nullptr; }
}

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

        // Ask Lua which combat script to use next
        static std::unique_ptr<LuaScript> flow;
        if (!flow) {
            flow = std::make_unique<LuaScript>(gameWorld, stateManager);
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

        const auto& cfg = GameConfig::get();
        int col = std::max(0, std::min(cfg.cols - 1, cfg.cols / 2));
        int row = 0; // player front row
        auto pos = gridToWorld(col, row);
        starter.position = pos;

        gameWorld->getPokemons().push_back(starter);
        std::cout << "[PlacementState] Moved starter to board at (" << starter.position.x << ", " << starter.position.z << ")\n";
    }
}

bool PlacementState::isValidGridPosition(const glm::vec3& position) const {
    const auto& cfg = GameConfig::get();

    float boardOriginX = -((cfg.cols * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;
    float boardOriginZ = -((cfg.rows * cfg.cellSize) / 2.0f) + cfg.cellSize * 0.5f;

    int col = static_cast<int>(std::round((position.x - boardOriginX) / cfg.cellSize));
    int row = static_cast<int>(std::round((position.z - boardOriginZ) / cfg.cellSize));

    if (col < 0 || col >= cfg.cols || row < 0 || row >= (cfg.rows / 2)) {
        return false;
    }

    glm::vec3 expected = gridToWorld(col, row);
    const float epsilon = 0.01f;
    bool xValid = std::abs(position.x - expected.x) < epsilon;
    bool zValid = std::abs(position.z - expected.z) < epsilon;

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
    const auto& cfg = GameConfig::get();
    int col = std::max(0, std::min(cfg.cols - 1, cfg.cols / 2));
    int row = 0; // player side front row
    starter.position = gridToWorld(col, row);
}
