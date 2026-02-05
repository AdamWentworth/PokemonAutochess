// ScriptedState.cpp

#include "game/scripting/ScriptedState.h"
#include "game/GameStateManager.h"
#include "game/GameConfig.h"
#include "game/state/PlacementState.h"
#include "engine/input/InputEvent.h"
#include <sol/sol.hpp>
#include <iostream>

static constexpr int UI_W = 1280;
static constexpr int UI_H = 720;

ScriptedState::ScriptedState(GameStateManager* manager, GameWorld* world, const std::string& path)
    : stateManager(manager)
    , gameWorld(world)
    , scriptPath(path)
    , script(world, manager)
{
    if (!script.loadScript(scriptPath)) {
        std::cerr << "[ScriptedState] Failed to load script: " << scriptPath << "\n";
    }
}

ScriptedState::~ScriptedState() = default;

void ScriptedState::ensureStarterUI() {
    if (uiInitialized) return;

    // Script functions/vars live in the script environment now.
    sol::table S = script.getScriptTable();

    bool hasCards = S["get_starter_cards"].valid();
    bool hasClick = S["on_card_click"].valid() || S["onCardClick"].valid();
    if (!(hasCards && hasClick)) {
        uiInitialized = true;
        return;
    }

    cardSystem.init();
    const auto& cfg = GameConfig::get();
    titleText = std::make_unique<TextRenderer>(cfg.fontPath, cfg.fontSize);

    sol::protected_function f = S["get_starter_cards"];
    sol::protected_function_result r = f();
    if (r.valid() && r.get_type() == sol::type::table) {
        std::vector<CardData> list;
        sol::table t = r;
        for (auto&& kv : t) {
            sol::table row = kv.second.as<sol::table>();
            CardData cd;

            auto nameOpt = row.get<sol::optional<std::string>>("name");
            cd.pokemonName = nameOpt.value_or(std::string());

            auto costOpt = row.get<sol::optional<int>>("cost");
            cd.cost = costOpt.value_or(0);

            auto typeOpt = row.get<sol::optional<std::string>>("type");
            std::string ty = typeOpt.value_or(std::string("Shop"));
            cd.type = (ty == "Starter") ? CardType::Starter : CardType::Shop;

            if (!cd.pokemonName.empty()) list.push_back(cd);
        }

        cardSystem.spawnCardRow(list, UI_W, /*y*/ 300);
        std::cout << "[ScriptedState] Spawned " << list.size() << " starter cards\n";
    } else {
        std::cerr << "[ScriptedState] get_starter_cards() did not return a table\n";
    }

    uiInitialized = true;
}

void ScriptedState::onEnter() {
    script.onEnter();
    ensureStarterUI();
}

void ScriptedState::onExit() {
    script.onExit();
}

void ScriptedState::handleInput(const InputEvent& event) {
    // Hot reload the script for fast iteration (press R).
    if (event.type == InputEvent::Type::KeyDown &&
        event.keyId == InputEvent::Key::R &&
        !event.repeat)
    {
        const bool ok = script.reload();
        std::cout << "[ScriptedState] Reload " << (ok ? "OK" : "FAILED") << "\n";

        // Rebuild starter UI if this script uses it.
        uiInitialized = false;
        cardSystem.clearCards();
        titleText.reset();
        ensureStarterUI();
        return; // avoid also sending this key into old script state
    }

    // If your scripts expect the event, you can add bindings later; keep current behavior.
    script.call("handleInput");

    if (!uiInitialized) return;

    sol::table S = script.getScriptTable();

    if (event.type == InputEvent::Type::MouseDown) {
        auto clicked = cardSystem.handleMouseClick(event.mouseX, event.mouseY);
        if (clicked) {
            sol::function onClick = S["on_card_click"];
            if (!onClick.valid()) onClick = S["onCardClick"];
            if (onClick.valid()) {
                onClick(clicked->pokemonName);
            }

            if (stateManager) {
                stateManager->pushState(std::make_unique<PlacementState>(
                    stateManager, gameWorld, clicked->pokemonName));
            }
        }
    }

    if (event.type == InputEvent::Type::KeyDown) {
        sol::function keyMap = S["handle_starter_key"];
        if (keyMap.valid()) {
            std::string key;
            switch (event.keyId) {
                case InputEvent::Key::Num1: key = "1"; break;
                case InputEvent::Key::Num2: key = "2"; break;
                case InputEvent::Key::Num3: key = "3"; break;
                default: break;
            }
            if (!key.empty()) {
                sol::protected_function_result r = keyMap(key);
                if (r.valid() && r.get_type() == sol::type::string) {
                    std::string pokemon = r.get<std::string>();

                    sol::function onClick = S["on_card_click"];
                    if (!onClick.valid()) onClick = S["onCardClick"];
                    if (onClick.valid()) onClick(pokemon);

                    if (stateManager) {
                        stateManager->pushState(std::make_unique<PlacementState>(
                            stateManager, gameWorld, pokemon));
                    }
                }
            }
        }
    }
}

void ScriptedState::update(float deltaTime) {
    script.onUpdate(deltaTime);
}

void ScriptedState::render() {
    script.call("onRender");

    if (!uiInitialized) return;

    sol::table S = script.getScriptTable();

    sol::function getMsg = S["get_message"];
    if (getMsg.valid() && titleText) {
        sol::protected_function_result r = getMsg();
        if (r.valid() && r.get_type() == sol::type::string) {
            std::string msg = r.get<std::string>();
            float w = titleText->measureTextWidth(msg, 1.0f);
            float x = (UI_W - w) * 0.5f;
            titleText->renderText(msg, x, 150.0f, {1.0f, 1.0f, 0.0f}, 1.0f);
        }
    }

    cardSystem.render(UI_W, UI_H);
}
