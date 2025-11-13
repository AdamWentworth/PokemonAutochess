// src/game/state/CombatState.cpp
#include "CombatState.h"
#include "../GameConfig.h"
#include "../GameWorld.h"
#include "../systems/MovementSystem.h"
#include "../systems/CombatSystem.h"

#include "../../engine/ui/TextRenderer.h"
#include "../LogBus.h"
#include <sol/sol.hpp>
#include <cmath>
#include <iostream>
#include <algorithm>

static std::string Capitalize(const std::string& s) {
    if (s.empty()) return s;
    std::string out = s;
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

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

    if (sol::function get_message = L["get_message"]; get_message.valid()) {
        if (auto r = get_message(); r.valid() && r.get_type() == sol::type::string) {
            combatMessage = r.get<std::string>();
        }
    }

    // Spawn enemies and emit plain encounter lines (no brackets/JSON)
    if (sol::function get_enemies = L["get_enemies"]; get_enemies.valid()) {
        sol::protected_function_result r = get_enemies();
        if (r.valid() && r.get_type() == sol::type::table) {
            sol::table enemies = r;
            for (auto&& kv : enemies) {
                sol::table e = kv.second.as<sol::table>();
                auto nameOpt = e.get<sol::optional<std::string>>("name");
                auto colOpt  = e.get<sol::optional<int>>("gridCol");
                auto rowOpt  = e.get<sol::optional<int>>("gridRow");
                auto lvlOpt  = e.get<sol::optional<int>>("level");
                if (nameOpt && colOpt && rowOpt) {
                    const int level = lvlOpt.value_or(-1);
                    gameWorld->spawnPokemonAtGrid(*nameOpt, *colOpt, *rowOpt, PokemonSide::Enemy, level);
                    LogBus::info("A wild " + Capitalize(*nameOpt) + " appeared!");
                }
            }
        }
    }

    // Player send-out lines
    {
        auto& units = gameWorld->getPokemons();
        for (auto& u : units) {
            if (!u.alive) continue;
            if (u.side == PokemonSide::Player) {
                LogBus::info("Go! " + Capitalize(u.name) + "!");
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

    const std::string& msg = combatMessage.empty() ? std::string("Combat") : combatMessage;
    float textWidth = textRenderer->measureTextWidth(msg, scale);
    float centeredX = std::round((windowWidth - textWidth) / 2.0f);
    textRenderer->renderText(msg, centeredX, 50.0f, glm::vec3(1.0f), scale);
}
