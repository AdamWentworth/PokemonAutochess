#include "PlacementState.h"

#include "CombatState.h"

#include "game/GameStateManager.h"
#include "game/GameWorld.h"
#include "game/GameServices.h"

#include "engine/ui/TextRenderer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <sol/sol.hpp>

namespace {
constexpr int UI_W = 1280;

} // namespace

const GameConfigData& PlacementState::cfg() const {
    if (services) return services->config;
    return fallbackConfig;
}

PlacementState::PlacementState(GameStateManager* manager, GameWorld* world, const std::string& name)
    : stateManager(manager)
    , gameWorld(world)
    , services(nullptr)
    , fallbackConfig(GameConfig::load(nullptr))
    , starterName(name)
{}

PlacementState::PlacementState(GameStateManager* manager, GameWorld* world, GameServices& svc, const std::string& name)
    : stateManager(manager)
    , gameWorld(world)
    , services(&svc)
    , fallbackConfig()
    , starterName(name)
{}

PlacementState::~PlacementState() = default;

void PlacementState::onEnter() {
    // no-op: player can drag unit; we only enforce validity + transition when timer expires
}

void PlacementState::onExit() {
    // no-op
}

void PlacementState::handleInput(const InputEvent&) {
    // Dragging/selection handled elsewhere in this project; keep no-op here.
}

void PlacementState::update(float dt) {
    if (!gameWorld || !stateManager) return;

    timer -= dt;

    if (timer > 0.0f || placementDone) return;
    placementDone = true;

    bool valid = false;
    auto& pokemons = gameWorld->getPokemons();
    auto it = std::find_if(pokemons.begin(), pokemons.end(),
        [&](const PokemonInstance& p) { return p.name == starterName; });

    if (it != pokemons.end()) {
        valid = isValidGridPosition(it->position);
    }

    if (!valid) {
        moveStarterToValidGridPosition();
    }

    // Ask Lua which combat script to use next
    static std::unique_ptr<LuaScript> flow;
    if (!flow) {
        LogBus::Logger* log = services ? &services->log : nullptr;
        flow = std::make_unique<LuaScript>(gameWorld, nullptr, log);
        flow->loadScript("scripts/states/flow.lua");
    }

    std::string routeScript = "scripts/states/route1.lua";

    // IMPORTANT: flow script functions live in its environment now.
    sol::table F = flow->getScriptTable();
    sol::function next_route = F["next_route_after_placement"];
    if (next_route.valid()) {
        sol::object r = next_route(starterName);
        if (r.is<std::string>()) routeScript = r.as<std::string>();
    }
    if (flow) flow->flushCommands();

    // NOTE: CombatState has both services and legacy ctors; use services if we have it.
    if (services) {
        stateManager->pushState(std::make_unique<CombatState>(stateManager, gameWorld, *services, routeScript));
    } else {
        stateManager->pushState(std::make_unique<CombatState>(stateManager, gameWorld, routeScript));
    }
}

void PlacementState::render() {
    if (!gameWorld) return;

    static std::unique_ptr<TextRenderer> text;
    if (!text) {
        const auto& c = cfg();
        text = std::make_unique<TextRenderer>(c.fontPath, c.fontSize);
    }

    const std::string message =
        "Place your starter! Time left: " + std::to_string(static_cast<int>(std::max(0.0f, timer)));

    const float scale = 1.0f;

    float textWidth = text->measureTextWidth(message, scale);
    float centeredX = std::round((UI_W - textWidth) / 2.0f);

    text->renderText(message, centeredX, 50.0f, glm::vec3(1.0f), scale);
}

bool PlacementState::isStarterOnBoard() const {
    if (!gameWorld) return false;
    const auto& pokemons = gameWorld->getPokemons();
    return std::any_of(pokemons.begin(), pokemons.end(),
        [&](const PokemonInstance& p) { return p.name == starterName; });
}

void PlacementState::moveStarterToBoard() {
    if (!gameWorld) return;

    auto& bench = gameWorld->getBenchPokemons();
    auto it = std::find_if(bench.begin(), bench.end(),
        [&](const PokemonInstance& p) { return p.name == starterName; });

    if (it == bench.end()) return;

    PokemonInstance starter = *it;
    bench.erase(it);

    // Default drop cell (col 3, row 0)
    placeOnValidGridPosition(starter);
    gameWorld->getPokemons().push_back(starter);
}

bool PlacementState::isValidGridPosition(const glm::vec3& position) const {
    const float cellSize = 1.2f;
    const float epsilon = 0.01f;

    float boardOriginX = -((8 * cellSize) / 2.0f) + cellSize * 0.5f;
    float boardOriginZ = cellSize * 0.5f;

    int col = static_cast<int>(std::round((position.x - boardOriginX) / cellSize));
    int row = static_cast<int>(std::round((position.z - boardOriginZ) / cellSize));

    // Player board = top 4 rows in this prototype
    if (col < 0 || col >= 8 || row < 0 || row >= 4) return false;

    float expectedX = boardOriginX + col * cellSize;
    float expectedZ = boardOriginZ + row * cellSize;

    bool xValid = std::abs(position.x - expectedX) < epsilon;
    bool zValid = std::abs(position.z - expectedZ) < epsilon;

    return xValid && zValid;
}

void PlacementState::moveStarterToValidGridPosition() {
    if (!gameWorld) return;

    auto& pokemons = gameWorld->getPokemons();
    auto& bench = gameWorld->getBenchPokemons();

    auto benchIt = std::find_if(bench.begin(), bench.end(),
        [&](const PokemonInstance& p) { return p.name == starterName; });

    if (benchIt != bench.end()) {
        PokemonInstance starter = *benchIt;
        bench.erase(benchIt);
        placeOnValidGridPosition(starter);
        pokemons.push_back(starter);
        return;
    }

    auto boardIt = std::find_if(pokemons.begin(), pokemons.end(),
        [&](const PokemonInstance& p) { return p.name == starterName; });

    if (boardIt != pokemons.end()) {
        placeOnValidGridPosition(*boardIt);
    } else {
        PokemonInstance starter;
        starter.name = starterName;
        placeOnValidGridPosition(starter);
        pokemons.push_back(starter);
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
