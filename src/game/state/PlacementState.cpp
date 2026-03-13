#include "PlacementState.h"

#include "CombatState.h"

#include "game/GameStateManager.h"
#include "game/GameWorld.h"
#include "game/GameServices.h"
#include "game/logging/FlowTrace.h"
#include "game/runtime/backend_ui/DebugText.h"
#include "game/runtime/backend_ui/TopBanner.h"
#include "game/runtime/routes/GameServiceRenderRoutes.h"
#include "game/state/BackendUiPolicy.h"
#include "game/ui/UIViewport.h"

#include "engine/render/IRenderBackend.h"
#include "engine/ui/TextRenderer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include <sol/sol.hpp>

PlacementState::PlacementState(GameStateManager* manager, GameWorld* world, GameServices& svc, const std::string& name)
    : stateManager(manager)
    , gameWorld(world)
    , services(svc)
    , starterName(name)
{}

PlacementState::~PlacementState() = default;

void PlacementState::onEnter() {
    // no-op: player can drag unit; we only enforce validity + transition when timer expires
    game::logging::flow::notePlacementStateEntered(starterName);
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
        flow = std::make_unique<LuaScript>(gameWorld,
                                           nullptr,
                                           services);
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

    stateManager->pushState(std::make_unique<CombatState>(stateManager, gameWorld, services, routeScript));
}

void PlacementState::render() {
    if (!gameWorld) return;

    const std::string message =
        "Place your starter! Time left: " + std::to_string(static_cast<int>(std::max(0.0f, timer)));

    const auto* viewport = services.viewport;
    const int uiW = viewport ? viewport->width : 1280;
    const int uiH = viewport ? viewport->height : 720;
    const auto routes = game::runtime::render::routesFromServices(services);
    const bool useBackendUi = game::state::backend_ui::shouldUseBackendUi(routes);

    if (useBackendUi) {
        if (!services.renderer) return;

        std::vector<IRenderBackend::DebugQuad> quads;
        quads.reserve(512);
        std::vector<IRenderBackend::DebugLine> lines;
        lines.reserve(2048);

        game::runtime::top_banner::Style style;
        style.textScale = 1.95f;
        style.panelG = 0.09f;
        style.panelA = 0.80f;
        game::runtime::top_banner::appendBackendBanner(
            quads, lines, uiW, uiH, message, 1.0f, 0.98f, 0.45f, style);

        services.renderer->drawDebugQuads(quads.data(), quads.size(), uiW, uiH);
        if (!lines.empty()) {
            services.renderer->drawDebugLines(lines.data(), lines.size(), uiW, uiH);
        }
        return;
    }

    static std::unique_ptr<TextRenderer> text;
    if (!text) {
        const auto& c = services.config;
        text = std::make_unique<TextRenderer>(c.fontPath, c.fontSize);
    }

    const float scale = 1.0f;
    float textWidth = text->measureTextWidth(message, scale);
    float centeredX = viewport ? viewport->centerX(textWidth)
                               : game::runtime::top_banner::centeredTextX(uiW, textWidth);
    const float textY = game::runtime::top_banner::topTextY(uiH);

    text->renderText(message, centeredX, textY, glm::vec3(1.0f), scale);
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
    if (!gameWorld) return false;
    const float cellSize = gameWorld->getBoardCellSize();
    const float epsilon = 0.01f;

    float boardOriginX = -((services.config.cols * cellSize) / 2.0f) + cellSize * 0.5f;
    float boardOriginZ = cellSize * 0.5f;

    int col = static_cast<int>(std::round((position.x - boardOriginX) / cellSize));
    int row = static_cast<int>(std::round((position.z - boardOriginZ) / cellSize));

    // Player board = top 4 rows in this prototype
    if (col < 0 || col >= services.config.cols || row < 0 || row >= 4) return false;

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
    if (!gameWorld) return;
    const float cellSize = gameWorld->getBoardCellSize();
    float boardOriginX = -((services.config.cols * cellSize) / 2.0f) + cellSize * 0.5f;
    float boardOriginZ = cellSize * 0.5f;

    int col = 3;
    int row = 0;

    starter.position.x = boardOriginX + col * cellSize;
    starter.position.z = boardOriginZ + row * cellSize;
    starter.position.y = 0.0f;
}


