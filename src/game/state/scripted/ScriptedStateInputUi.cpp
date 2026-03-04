#include "ScriptedState.h"

#include "game/GameStateManager.h"
#include "game/logging/FlowTrace.h"
#include "game/scripting/LuaScriptHelpers.h"
#include "game/state/PlacementState.h"

#include <sol/sol.hpp>
#include <algorithm>
#include <iostream>

namespace {
constexpr float kBackendTextScaleBaseForHit = 1.35f;

bool isPointInsideTextMenuEntryHitBox(float entryX,
                                      float entryY,
                                      float entryW,
                                      float entryH,
                                      float entryScale,
                                      int mouseX,
                                      int mouseY,
                                      float backendTextMenuScale) {
    const float textScale =
        std::max(0.1f, entryScale) * kBackendTextScaleBaseForHit * std::max(0.55f, backendTextMenuScale);
    const float padX = std::max(8.0f, 10.0f * textScale * 0.5f);
    const float padY = std::max(4.0f, 6.0f * textScale * 0.5f);
    const float left = entryX - padX;
    const float top = entryY - padY;
    const float right = entryX + entryW + padX;
    const float bottom = entryY + entryH + padY;
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);
    return mx >= left && mx <= right && my >= top && my <= bottom;
}
} // namespace

void ScriptedState::handleInput(const InputEvent& event) {
    if (event.type == InputEvent::Type::Resize) {
        if (uiInitialized) {
            if (cardMode == CardMode::TextMenu) {
                rebuildTextMenu();
            } else {
                rebuildCardRow();
            }
        }
    }

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
        itemCardSystem.clearCards();
        textMenuEntries.clear();
        titleText.reset();
        shopUi.reset();
        cardMode = CardMode::None;
        renderWorld = true;
        hasShopReadyButton = false;
        hasShopRerollButton = false;
        clearBackendShopUiCache();
        ensureCardUI();
        return; // avoid also sending this key into old script state
    }

    // If your scripts expect the event, you can add bindings later; keep current behavior.
    script.call("handleInput");

    if (!uiInitialized) return;
    if (cardMode == CardMode::TextMenu &&
        event.type == InputEvent::Type::KeyDown &&
        !event.repeat) {
        if (event.keyId == InputEvent::Key::Escape) {
            sol::table S = script.getScriptTable();
            sol::function onMenuBack = game::scripting::resolveFunction(S, {"on_text_menu_back", "on_menu_back"});
            if (onMenuBack.valid()) {
                onMenuBack();
                script.flushCommands();
                rebuildTextMenu();
                return;
            }
        }
        if (tryHandleHeadlessTextMenuKey(event.keyId)) {
            return;
        }
    }
    if (shouldUseBackendCardUi() &&
        (cardMode == CardMode::Shop || cardMode == CardMode::Starter) &&
        event.type == InputEvent::Type::KeyDown &&
        !event.repeat) {
        if (tryHandleBackendCardKey(event.keyId)) {
            return;
        }
    }
    const bool uiSelectionMode =
        (cardMode == CardMode::TextMenu || cardMode == CardMode::Starter || cardMode == CardMode::Shop);
    if (event.type == InputEvent::Type::MouseDown && gameWorld && !uiSelectionMode) {
        if (gameWorld->consumeUiClickBlocked()) return;
        if (gameWorld->isUnitDragActive()) return;
    }

    sol::table S = script.getScriptTable();

    if (event.type == InputEvent::Type::MouseDown) {
        if (cardMode == CardMode::TextMenu) {
            for (const auto& entry : textMenuEntries) {
                if (!entry.enabled) continue;
                const std::string selectedEntryId = entry.id;
                if (!isPointInsideTextMenuEntryHitBox(
                        entry.x,
                        entry.y,
                        entry.w,
                        entry.h,
                        entry.scale,
                        event.mouseX,
                        event.mouseY,
                        backendTextMenuScale)) {
                    continue;
                }

                const bool startActionEntry = game::logging::flow::isStartActionEntry(selectedEntryId);
                if (startActionEntry) {
                    game::logging::flow::noteMenuActionClick(selectedEntryId, scriptPath);
                }
                const double tLuaStart = game::logging::flow::nowMs();
                sol::function onMenuClick = game::scripting::resolveFunction(S, {"on_text_menu_click", "on_menu_click"});
                if (onMenuClick.valid()) {
                    onMenuClick(selectedEntryId);
                }
                const double tLuaEnd = game::logging::flow::nowMs();
                script.flushCommands();
                const double tFlushEnd = game::logging::flow::nowMs();
                rebuildTextMenu();
                const double tRebuildEnd = game::logging::flow::nowMs();
                if (startActionEntry) {
                    game::logging::flow::log(
                        "menu_click_pipeline",
                        "entry=" + selectedEntryId +
                        " lua=" + game::logging::flow::formatMs(tLuaEnd - tLuaStart) +
                        " flush=" + game::logging::flow::formatMs(tFlushEnd - tLuaEnd) +
                        " rebuild=" + game::logging::flow::formatMs(tRebuildEnd - tFlushEnd));
                }
                return;
            }
        }

        if (shouldUseBackendCardUi() &&
            (cardMode == CardMode::Shop || cardMode == CardMode::Starter)) {
            if (handleBackendCardMouseClick(event.mouseX, event.mouseY)) {
                return;
            }
        }

        if (cardMode == CardMode::Shop && hasShopReadyButton) {
            const bool insideReadyX = static_cast<float>(event.mouseX) >= shopReadyX &&
                                      static_cast<float>(event.mouseX) <= (shopReadyX + shopReadyW);
            const bool insideReadyY = static_cast<float>(event.mouseY) >= shopReadyY &&
                                      static_cast<float>(event.mouseY) <= (shopReadyY + shopReadyH);
            if (insideReadyX && insideReadyY) {
                sol::function onReady = game::scripting::resolveFunction(S, {"on_shop_ready_click"});
                if (onReady.valid()) {
                    onReady();
                }
                script.flushCommands();
                rebuildCardRow();
                return;
            }
        }

        if (cardMode == CardMode::Shop && shopUi) {
            const game::ui::ShopUiClickResult click = shopUi->handleMouseDown(event.mouseX, event.mouseY);
            if (click.rerollClicked && hasShopRerollButton) {
                sol::function onReroll = game::scripting::resolveFunction(S, {"on_shop_reroll_click"});
                if (onReroll.valid()) {
                    onReroll();
                }
                script.flushCommands();
                rebuildCardRow();
                return;
            }
            if (click.cardClicked) {
                sol::function onClick = game::scripting::resolveFunction(S, {"on_shop_card_click", "on_card_click", "onCardClick"});
                if (onClick.valid()) {
                    onClick(click.cardClicked->pokemonName, click.cardClicked->level);
                }
                script.flushCommands();
                rebuildCardRow();
                return;
            }
        } else {
            auto clicked = cardSystem.handleMouseClick(event.mouseX, event.mouseY);
            if (clicked && cardMode == CardMode::Starter) {
                game::logging::flow::noteStarterCardClick(clicked->pokemonName);
                const double tLuaStart = game::logging::flow::nowMs();
                sol::function onClick = game::scripting::resolveFunction(S, {"on_card_click", "onCardClick"});
                if (onClick.valid()) {
                    onClick(clicked->pokemonName);
                }
                const double tLuaEnd = game::logging::flow::nowMs();
                script.flushCommands();
                const double tFlushEnd = game::logging::flow::nowMs();

                if (stateManager) {
                    stateManager->pushState(std::make_unique<PlacementState>(
                        stateManager, gameWorld, services, clicked->pokemonName));
                }
                const double tPushEnd = game::logging::flow::nowMs();
                game::logging::flow::log(
                    "starter_click_pipeline",
                    "pokemon=" + clicked->pokemonName +
                    " lua=" + game::logging::flow::formatMs(tLuaEnd - tLuaStart) +
                    " flush=" + game::logging::flow::formatMs(tFlushEnd - tLuaEnd) +
                    " push_placement=" + game::logging::flow::formatMs(tPushEnd - tFlushEnd));
            }
        }
        if (cardMode == CardMode::Shop && hasShopItems) {
            auto itemClicked = itemCardSystem.handleMouseClick(event.mouseX, event.mouseY);
            if (itemClicked) {
                sol::function onItemClick = game::scripting::resolveFunction(S, {"on_shop_item_click"});
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

                        game::logging::flow::noteStarterCardClick(pokemon);
                        const double tLuaStart = game::logging::flow::nowMs();
                        sol::function onClick = game::scripting::resolveFunction(S, {"on_card_click", "onCardClick"});
                        if (onClick.valid()) onClick(pokemon);
                        const double tLuaEnd = game::logging::flow::nowMs();
                        script.flushCommands();
                        const double tFlushEnd = game::logging::flow::nowMs();

                        if (stateManager) {
                            stateManager->pushState(std::make_unique<PlacementState>(
                                stateManager, gameWorld, services, pokemon));
                        }
                        const double tPushEnd = game::logging::flow::nowMs();
                        game::logging::flow::log(
                            "starter_key_pipeline",
                            "pokemon=" + pokemon +
                            " lua=" + game::logging::flow::formatMs(tLuaEnd - tLuaStart) +
                            " flush=" + game::logging::flow::formatMs(tFlushEnd - tLuaEnd) +
                            " push_placement=" + game::logging::flow::formatMs(tPushEnd - tFlushEnd));
                    }
                }
            }
        }
    }
}

