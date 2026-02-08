#include "CombatState.h"

#include "game/GameWorld.h"
#include "game/GameServices.h"
#include "game/logging/LoggerUtil.h"

#include "game/ecs/CombatActive.h"
#include "engine/core/ecs/World.h"

#include "engine/ui/TextRenderer.h"

#include <algorithm>
#include <cmath>
#include <sol/sol.hpp>

namespace {
constexpr int UI_W = 1280;

std::string Capitalize(std::string s) {
    if (s.empty()) return s;
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}
} // namespace

CombatState::CombatState(GameStateManager* manager, GameWorld* world, GameServices& svc, const std::string& path)
    : stateManager(manager)
    , gameWorld(world)
    , services(svc)
    , script(world, manager, svc)
    , combatMessage()
{
    if (!script.loadScript(path)) {
        game::log::error(&services.log, std::string("[CombatState] Failed to load combat script: ") + path);
    }

}

CombatState::~CombatState() = default;

void CombatState::onEnter() {
    if (services.ecsWorld && services.ecsWorld->alive(services.combatStateEntity)) {
        if (auto* state = services.ecsWorld->get<game::CombatActive>(services.combatStateEntity)) {
            state->active = true;
        } else {
            services.ecsWorld->add<game::CombatActive>(services.combatStateEntity, game::CombatActive{true});
        }
    }

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
                    game::log::info(&services.log, "A wild " + Capitalize(*nameOpt) + " appeared!");
                }
            }
        }
    }
    script.flushCommands();

    // Player send-out lines (optional flavor)
    for (auto& u : gameWorld->getPokemons()) {
        if (!u.alive) continue;
        if (u.side == PokemonSide::Player) {
            game::log::info(&services.log, "Go! " + Capitalize(u.name) + "!");
        }
    }

    script.onEnter();
}

void CombatState::onExit() {
    if (services.ecsWorld && services.ecsWorld->alive(services.combatStateEntity)) {
        if (auto* state = services.ecsWorld->get<game::CombatActive>(services.combatStateEntity)) {
            state->active = false;
        }
    }
    script.onExit();
}

void CombatState::handleInput(const InputEvent&) {}

void CombatState::update(float dt) {
    script.onUpdate(dt);
}

void CombatState::render() {
    if (!textRenderer) {
        const auto& c = services.config;
        textRenderer = std::make_unique<TextRenderer>(c.fontPath, c.fontSize);
    }
    if (!textRenderer) return;

    const float scale = 1.0f;
    const std::string msg = combatMessage.empty() ? std::string("Combat") : combatMessage;

    float textWidth = textRenderer->measureTextWidth(msg, scale);
    float centeredX = std::round((UI_W - textWidth) / 2.0f);

    textRenderer->renderText(msg, centeredX, 50.0f, glm::vec3(1.0f), scale);
}
