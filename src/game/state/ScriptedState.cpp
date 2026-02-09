#include "ScriptedState.h"

#include "game/GameStateManager.h"
#include "game/GameServices.h"
#include "game/state/PlacementState.h"
#include "game/ui/UIViewport.h"
#include "engine/input/InputEvent.h"

#include <sol/sol.hpp>
#include <iostream>

ScriptedState::ScriptedState(GameStateManager* manager, GameWorld* world, GameServices& svc, const std::string& path)
    : stateManager(manager)
    , gameWorld(world)
    , services(svc)
    , scriptPath(path)
    , script(world, manager, svc)
{
    if (!script.loadScript(scriptPath)) {
        std::cerr << "[ScriptedState] Failed to load script: " << scriptPath << "\n";
    }
}

ScriptedState::~ScriptedState() = default;

void ScriptedState::rebuildCardRow() {
    sol::table S = script.getScriptTable();
    sol::protected_function f;

    auto buildList = [&](sol::protected_function fn, std::vector<CardData>& out) {
        out.clear();
        if (!fn.valid()) return false;

        sol::protected_function_result r = fn();
        if (!(r.valid() && r.get_type() == sol::type::table)) {
            std::cerr << "[ScriptedState] card list function did not return a table\n";
            return false;
        }

        sol::table t = r;
        for (auto&& kv : t) {
            sol::table row = kv.second.as<sol::table>();
            CardData cd;

            auto nameOpt = row.get<sol::optional<std::string>>("name");
            cd.pokemonName = nameOpt.value_or(std::string());

            auto costOpt = row.get<sol::optional<int>>("cost");
            cd.cost = costOpt.value_or(0);

            auto levelOpt = row.get<sol::optional<int>>("level");
            cd.level = levelOpt.value_or(0);

            auto labelOpt = row.get<sol::optional<std::string>>("label");
            cd.label = labelOpt.value_or(std::string());

            auto imageOpt = row.get<sol::optional<std::string>>("image");
            cd.imagePath = imageOpt.value_or(std::string());

            auto uvOpt = row.get<sol::optional<sol::table>>("uv");
            if (uvOpt) {
                sol::table uv = *uvOpt;
                auto u0 = uv.get<sol::optional<float>>(1);
                auto v0 = uv.get<sol::optional<float>>(2);
                auto u1 = uv.get<sol::optional<float>>(3);
                auto v1 = uv.get<sol::optional<float>>(4);
                if (u0 && v0 && u1 && v1) {
                    cd.uvMin = { *u0, *v0 };
                    cd.uvMax = { *u1, *v1 };
                }
            }

            auto typeOpt = row.get<sol::optional<std::string>>("type");
            std::string ty = typeOpt.value_or(std::string("Shop"));
            if (ty == "Starter") cd.type = CardType::Starter;
            else if (ty == "Item") cd.type = CardType::Item;
            else cd.type = CardType::Shop;

            if (!cd.pokemonName.empty() || !cd.label.empty()) out.push_back(cd);
        }
        return true;
    };

    std::vector<CardData> list;
    if (cardMode == CardMode::Shop) {
        f = S["get_shop_cards"];
    } else if (cardMode == CardMode::Starter) {
        f = S["get_starter_cards"];
    }
    if (!buildList(f, list)) return;

    const auto* viewport = services.viewport;
    const int uiW = viewport ? viewport->width : 1280;
    const int uiH = viewport ? viewport->height : 720;
    if (cardMode == CardMode::Shop) {
        const int cardW = 160;
        const int cardH = 110;
        const int spacing = 20;
        const int margin = 40;
        const int y = std::max(0, uiH - cardH - margin);
        cardSystem.spawnCardRowLayout(list, uiW, y, cardW, cardH, spacing);
        if (hasShopItems) {
            sol::protected_function itemFn = S["get_shop_items"];
            std::vector<CardData> items;
            if (buildList(itemFn, items)) {
                const int itemW = 96;
                const int itemH = 96;
                const int itemSpacing = 14;
                const int itemMargin = 32;
                const int itemY = std::max(0, itemMargin);
                itemCardSystem.spawnCardRowLayout(items, uiW, itemY, itemW, itemH, itemSpacing);
            }
        }
    } else {
        cardSystem.spawnCardRow(list, uiW, /*y*/ 300);
    }
    std::cout << "[ScriptedState] Spawned " << list.size() << " cards\n";
}

void ScriptedState::ensureCardUI() {
    if (uiInitialized) return;
    if (!services.renderEnabled) {
        cardMode = CardMode::None;
        uiInitialized = true;
        return;
    }

    // Script functions/vars live in the script environment now.
    sol::table S = script.getScriptTable();

    bool hasShopCards = S["get_shop_cards"].valid();
    bool hasShopClick = S["on_shop_card_click"].valid() || S["on_card_click"].valid() || S["onCardClick"].valid();
    bool hasStarterCards = S["get_starter_cards"].valid();
    bool hasStarterClick = S["on_card_click"].valid() || S["onCardClick"].valid();
    hasShopItems = S["get_shop_items"].valid() && S["on_shop_item_click"].valid();

    if (hasShopCards && hasShopClick) {
        cardMode = CardMode::Shop;
    } else if (hasStarterCards && hasStarterClick) {
        cardMode = CardMode::Starter;
    } else {
        cardMode = CardMode::None;
        uiInitialized = true;
        return;
    }

    cardSystem.init();
    const auto& c = services.config;
    cardSystem.initOverlayText(c.fontPath, std::max(14, c.fontSize / 3));
    if (hasShopItems) {
        itemCardSystem.init();
        itemCardSystem.initOverlayText(c.fontPath, std::max(14, c.fontSize / 3));
    }
    titleText = std::make_unique<TextRenderer>(c.fontPath, c.fontSize);

    rebuildCardRow();

    uiInitialized = true;
    script.flushCommands();
}

void ScriptedState::onEnter() {
    script.onEnter();
    ensureCardUI();
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
        cardMode = CardMode::None;
        ensureCardUI();
        return; // avoid also sending this key into old script state
    }

    // If your scripts expect the event, you can add bindings later; keep current behavior.
    script.call("handleInput");

    if (!uiInitialized) return;

    sol::table S = script.getScriptTable();

    if (event.type == InputEvent::Type::MouseDown) {
        auto clicked = cardSystem.handleMouseClick(event.mouseX, event.mouseY);
        if (clicked) {
            if (cardMode == CardMode::Shop) {
                sol::function onClick = S["on_shop_card_click"];
                if (!onClick.valid()) onClick = S["on_card_click"];
                if (!onClick.valid()) onClick = S["onCardClick"];
                if (onClick.valid()) {
                    onClick(clicked->pokemonName, clicked->level);
                }
                script.flushCommands();
                rebuildCardRow();
            } else if (cardMode == CardMode::Starter) {
                sol::function onClick = S["on_card_click"];
                if (!onClick.valid()) onClick = S["onCardClick"];
                if (onClick.valid()) {
                    onClick(clicked->pokemonName);
                }
                script.flushCommands();

                if (stateManager) {
                    stateManager->pushState(std::make_unique<PlacementState>(
                        stateManager, gameWorld, services, clicked->pokemonName));
                }
            }
        }
        if (cardMode == CardMode::Shop && hasShopItems) {
            auto itemClicked = itemCardSystem.handleMouseClick(event.mouseX, event.mouseY);
            if (itemClicked) {
                sol::function onItemClick = S["on_shop_item_click"];
                if (onItemClick.valid()) {
                    onItemClick(itemClicked->pokemonName, itemClicked->cost);
                }
                script.flushCommands();
                rebuildCardRow();
            }
        }
    }

    if (event.type == InputEvent::Type::KeyDown) {
        if (cardMode == CardMode::Starter) {
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
                        script.flushCommands();

                        if (stateManager) {
                            stateManager->pushState(std::make_unique<PlacementState>(
                                stateManager, gameWorld, services, pokemon));
                        }
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
    const auto* viewport = services.viewport;
    const int uiW = viewport ? viewport->width : 1280;
    const int uiH = viewport ? viewport->height : 720;

    sol::function getMsg = S["get_message"];
    if (getMsg.valid() && titleText) {
        sol::protected_function_result r = getMsg();
        if (r.valid() && r.get_type() == sol::type::string) {
            std::string msg = r.get<std::string>();
            float w = titleText->measureTextWidth(msg, 1.0f);
            float x = (static_cast<float>(uiW) - w) * 0.5f;
            titleText->renderText(msg, x, 150.0f, {1.0f, 1.0f, 0.0f}, 1.0f);
        }
    }

    cardSystem.render(uiW, uiH);
    if (cardMode == CardMode::Shop && hasShopItems) {
        itemCardSystem.render(uiW, uiH);
    }
}
