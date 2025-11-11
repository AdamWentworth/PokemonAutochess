// ScriptedState.cpp

#include "game/ScriptedState.h"
#include "game/GameStateManager.h"
#include "game/GameConfig.h"
#include "game/state/PlacementState.h"   // NEW: push old placement flow after click
#include <SDL2/SDL.h>
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

    // Only build the UI if the script provides the expected functions.
    sol::state& L = script.getState();
    bool hasCards = L["get_starter_cards"].valid();
    bool hasClick = L["on_card_click"].valid() || L["onCardClick"].valid(); // support both
    if (!(hasCards && hasClick)) {
        // Not a starter-style script; nothing to do.
        uiInitialized = true; // prevent re-check each frame
        return;
    }

    // Init UI + font
    cardSystem.init();
    const auto& cfg = GameConfig::get();
    titleText = std::make_unique<TextRenderer>(cfg.fontPath, cfg.fontSize);

    // Ask Lua for starter cards (array of { name, cost, type })
    sol::protected_function f = L["get_starter_cards"];
    sol::protected_function_result r = f();
    if (r.valid() && r.get_type() == sol::type::table) {
        std::vector<CardData> list;
        sol::table t = r;
        for (auto&& kv : t) {
            sol::table row = kv.second.as<sol::table>();
            CardData cd;

            // Use sol::optional to avoid MSVC ambiguity with get_or overloads
            auto nameOpt = row.get<sol::optional<std::string>>("name");
            cd.pokemonName = nameOpt.value_or(std::string());

            auto costOpt = row.get<sol::optional<int>>("cost");
            cd.cost = costOpt.value_or(0);

            auto typeOpt = row.get<sol::optional<std::string>>("type");
            std::string ty = typeOpt.value_or(std::string("Shop"));
            cd.type = (ty == "Starter") ? CardType::Starter : CardType::Shop;

            if (!cd.pokemonName.empty()) list.push_back(cd);
        }

        // 🔄 Match the old (stable) visual layout:
        //   - Larger cards (handled inside CardSystem)
        //   - Y offset around 300
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

void ScriptedState::handleInput(SDL_Event& event) {
    // Forward to Lua (scripts that care about raw input can implement handleInput)
    script.call("handleInput");

    if (!uiInitialized) return;

    sol::state& L = script.getState();

    // Mouse click → hit test cards → on_card_click(name)
    if (event.type == SDL_MOUSEBUTTONDOWN) {
        auto clicked = cardSystem.handleMouseClick(event.button.x, event.button.y);
        if (clicked) {
            // Call either on_card_click or legacy onCardClick if present
            sol::function onClick = L["on_card_click"];
            if (!onClick.valid()) onClick = L["onCardClick"];
            if (onClick.valid()) {
                onClick(clicked->pokemonName);
            }

            // ✅ Restore stable flow: after a selection, go to PlacementState,
            // which then advances to Route 1 / Combat just like before.
            if (stateManager) {
                stateManager->pushState(std::make_unique<PlacementState>(
                    stateManager, gameWorld, clicked->pokemonName));
            }
        }
    }

    // Keyboard shortcuts via optional handle_starter_key(key)
    if (event.type == SDL_KEYDOWN) {
        sol::function keyMap = L["handle_starter_key"];
        if (keyMap.valid()) {
            std::string key;
            switch (event.key.keysym.sym) {
                case SDLK_1: key = "1"; break;
                case SDLK_2: key = "2"; break;
                case SDLK_3: key = "3"; break;
                default: break;
            }
            if (!key.empty()) {
                sol::protected_function_result r = keyMap(key);
                if (r.valid() && r.get_type() == sol::type::string) {
                    std::string pokemon = r.get<std::string>();
                    sol::function onClick = L["on_card_click"];
                    if (!onClick.valid()) onClick = L["onCardClick"];
                    if (onClick.valid()) onClick(pokemon);

                    // Same as mouse flow
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
    // Allow the Lua script to render if it wants.
    script.call("onRender");

    if (!uiInitialized) return;

    sol::state& L = script.getState();

    // Optional title text via get_message()
    sol::function getMsg = L["get_message"];
    if (getMsg.valid() && titleText) {
        sol::protected_function_result r = getMsg();
        if (r.valid() && r.get_type() == sol::type::string) {
            std::string msg = r.get<std::string>();
            float w = titleText->measureTextWidth(msg, 1.0f);
            float x = (UI_W - w) * 0.5f;
            // 🔄 Match old placement for title (around 150 px)
            titleText->renderText(msg, x, 150.0f, {1.0f, 1.0f, 0.0f}, 1.0f);
        }
    }

    // Draw cards
    cardSystem.render(UI_W, UI_H);
}
