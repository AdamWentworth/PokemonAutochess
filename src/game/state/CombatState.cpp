// src/game/state/CombatState.cpp
#include "CombatState.h"
#include "../GameConfig.h"
#include "../GameWorld.h"
#include "../systems/MovementSystem.h"
#include "../systems/CombatSystem.h"
#include <iostream>
#include <sol/sol.hpp>

CombatState::CombatState(GameStateManager* manager, GameWorld* world, const std::string& scriptPath)
    : stateManager(manager)
    , gameWorld(world)
    , script(world, manager)
{
    const auto& cfg = GameConfig::get();
    textRenderer = std::make_unique<TextRenderer>(cfg.fontPath, cfg.fontSize);

    if (!script.loadScript(scriptPath)) {
        std::cerr << "[CombatState] Failed to load combat script: " << scriptPath << "\n";
    }

    movementSystem = std::make_unique<MovementSystem>(gameWorld);
    combatSystem   = std::make_unique<CombatSystem>(gameWorld);
}

CombatState::~CombatState() = default;

void CombatState::onEnter() {
    sol::state& L = script.getState();

    // Message: route1.lua exposes get_message()
    sol::function get_message = L["get_message"];
    if (get_message.valid()) {
        sol::protected_function_result r = get_message();
        if (r.valid() && r.get_type() == sol::type::string) {
            combatMessage = r.get<std::string>();
        }
    }

    // Enemies: route1.lua exposes get_enemies() (array of {name, gridCol, gridRow})
    sol::function get_enemies = L["get_enemies"];
    if (get_enemies.valid()) {
        sol::protected_function_result r = get_enemies();
        if (r.valid() && r.get_type() == sol::type::table) {
            sol::table enemies = r;
            for (auto&& kv : enemies) {
                sol::table e = kv.second.as<sol::table>();
                auto nameOpt = e.get<sol::optional<std::string>>("name");
                auto colOpt  = e.get<sol::optional<int>>("gridCol");
                auto rowOpt  = e.get<sol::optional<int>>("gridRow");
                if (nameOpt && colOpt && rowOpt) {
                    gameWorld->spawnPokemonAtGrid(*nameOpt, *colOpt, *rowOpt, PokemonSide::Enemy);
                }
            }
        }
    }

    script.onEnter();
}

void CombatState::onExit() {
    script.onExit();
}

void CombatState::handleInput(SDL_Event& event) {
    (void)event;
}

void CombatState::update(float deltaTime) {
    script.onUpdate(deltaTime);
    if (movementSystem) movementSystem->update(deltaTime);
    if (combatSystem)   combatSystem->update(deltaTime);
}

void CombatState::render() {
    if (!textRenderer) return;
    const float scale = 1.0f;
    const int windowWidth = 1280;

    std::string msg = combatMessage.empty() ? "Combat" : combatMessage;
    float textWidth = textRenderer->measureTextWidth(msg, scale);
    float centeredX = std::round((windowWidth - textWidth) / 2.0f);
    textRenderer->renderText(msg, centeredX, 50.0f, glm::vec3(1.0f), scale);
}
