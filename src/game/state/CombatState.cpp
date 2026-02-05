#include "CombatState.h"

#include "game/GameWorld.h"
#include "game/GameServices.h"
#include "game/GameConfig.h"
#include "game/logging/LogBus.h"

#include "game/systems/MovementSystem.h"
#include "game/systems/CombatSystem.h"

#include "engine/ui/TextRenderer.h"

#include <algorithm>
#include <cmath>
#include <sol/sol.hpp>

namespace {
constexpr int UI_W = 1280;

const GameConfigData& cfgOrLegacy(const GameServices* services) {
    if (services) return services->config;
    return GameConfig::get(); // legacy fallback
}

std::string Capitalize(std::string s) {
    if (s.empty()) return s;
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}
} // namespace

CombatState::CombatState(GameStateManager* manager, GameWorld* world, const std::string& path)
    : stateManager(manager)
    , gameWorld(world)
    , services(nullptr)
    , script(world, manager)
    , combatMessage()
{
    const auto& c = cfgOrLegacy(services);
    textRenderer = std::make_unique<TextRenderer>(c.fontPath, c.fontSize);

    if (!script.loadScript(path)) {
        LogBus::error(std::string("[CombatState] Failed to load combat script: ") + path);
    }

    movementSystem = std::make_unique<MovementSystem>(gameWorld);
    combatSystem   = std::make_unique<CombatSystem>(gameWorld);
}

CombatState::CombatState(GameStateManager* manager, GameWorld* world, GameServices& svc, const std::string& path)
    : stateManager(manager)
    , gameWorld(world)
    , services(&svc)
    , script(world, manager)
    , combatMessage()
{
    const auto& c = cfgOrLegacy(services);
    textRenderer = std::make_unique<TextRenderer>(c.fontPath, c.fontSize);

    if (!script.loadScript(path)) {
        LogBus::error(std::string("[CombatState] Failed to load combat script: ") + path);
    }

    movementSystem = std::make_unique<MovementSystem>(gameWorld);
    combatSystem   = std::make_unique<CombatSystem>(gameWorld);
}

CombatState::~CombatState() = default;

void CombatState::onEnter() {
    sol::table S = script.getScriptTable();

    if (sol::function get_message = S["get_message"]; get_message.valid()) {
        if (auto r = get_message(); r.valid() && r.get_type() == sol::type::string) {
            combatMessage = r.get<std::string>();
        }
    }

    if (sol::function get_enemies = S["get_enemies"]; get_enemies.valid()) {
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

    // Player send-out lines (optional flavor)
    for (auto& u : gameWorld->getPokemons()) {
        if (!u.alive) continue;
        if (u.side == PokemonSide::Player) {
            LogBus::info("Go! " + Capitalize(u.name) + "!");
        }
    }

    script.onEnter();
}

void CombatState::onExit() { script.onExit(); }

void CombatState::handleInput(const InputEvent&) {}

void CombatState::update(float dt) {
    script.onUpdate(dt);
    if (movementSystem) movementSystem->update(dt);
    if (combatSystem)   combatSystem->update(dt);
}

void CombatState::render() {
    if (!textRenderer) return;

    const float scale = 1.0f;
    const std::string msg = combatMessage.empty() ? std::string("Combat") : combatMessage;

    float textWidth = textRenderer->measureTextWidth(msg, scale);
    float centeredX = std::round((UI_W - textWidth) / 2.0f);

    textRenderer->renderText(msg, centeredX, 50.0f, glm::vec3(1.0f), scale);
}
