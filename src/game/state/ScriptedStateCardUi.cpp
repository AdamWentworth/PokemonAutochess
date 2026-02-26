#include "ScriptedState.h"

#include "game/scripting/LuaCardParser.h"
#include "game/scripting/LuaScriptHelpers.h"
#include "game/runtime/GameServiceRenderRoutes.h"
#include "game/state/BackendUiPolicy.h"
#include "game/state/ShopCardConversion.h"
#include "game/ui/ShopLayout.h"
#include "game/ui/UIViewport.h"

#include <sol/sol.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
struct ScriptedUiCapabilities {
    bool hasTextMenuEntries = false;
    bool hasTextMenuClick = false;
    bool hasShopCards = false;
    bool hasShopClick = false;
    bool hasStarterCards = false;
    bool hasStarterClick = false;
    bool hasShopItems = false;
    bool hasShopReadyButton = false;
    bool hasShopRerollButton = false;
    bool hasTextMenu = false;
    bool renderWorld = true;
};

ScriptedUiCapabilities readScriptedUiCapabilities(sol::table scriptTable) {
    ScriptedUiCapabilities out;
    out.hasTextMenuEntries = game::scripting::hasFunction(scriptTable, "get_text_menu_entries");
    out.hasTextMenuClick = game::scripting::hasAnyFunction(scriptTable, {"on_text_menu_click", "on_menu_click"});
    out.hasShopCards = game::scripting::hasFunction(scriptTable, "get_shop_cards");
    out.hasShopClick = game::scripting::hasAnyFunction(scriptTable, {"on_shop_card_click", "on_card_click", "onCardClick"});
    out.hasStarterCards = game::scripting::hasFunction(scriptTable, "get_starter_cards");
    out.hasStarterClick = game::scripting::hasAnyFunction(scriptTable, {"on_card_click", "onCardClick"});
    out.hasShopItems = game::scripting::hasFunction(scriptTable, "get_shop_items") &&
                       game::scripting::hasFunction(scriptTable, "on_shop_item_click");
    out.hasShopReadyButton = game::scripting::hasFunction(scriptTable, "on_shop_ready_click");
    out.hasShopRerollButton = game::scripting::hasFunction(scriptTable, "on_shop_reroll_click");
    out.hasTextMenu = out.hasTextMenuEntries && out.hasTextMenuClick;
    if (auto hideWorld = scriptTable.get<sol::optional<bool>>("hide_world")) {
        out.renderWorld = !(*hideWorld);
    }
    return out;
}

enum class ScriptedUiMode {
    None,
    Starter,
    Shop,
    TextMenu,
};

ScriptedUiMode chooseScriptedUiMode(const ScriptedUiCapabilities& caps) {
    if (caps.hasTextMenu) return ScriptedUiMode::TextMenu;
    if (caps.hasShopCards && caps.hasShopClick) return ScriptedUiMode::Shop;
    if (caps.hasStarterCards && caps.hasStarterClick) return ScriptedUiMode::Starter;
    return ScriptedUiMode::None;
}

} // namespace

void ScriptedState::rebuildCardRow() {
    sol::table S = script.getScriptTable();
    sol::protected_function f;

    std::vector<CardData> list;
    if (cardMode == CardMode::Shop) {
        f = S["get_shop_cards"];
    } else if (cardMode == CardMode::Starter) {
        f = S["get_starter_cards"];
    }
    std::string parseError;
    if (!game::scripting::parseCardList(f, list, &parseError)) {
        std::cerr << "[ScriptedState] failed to parse card list: " << parseError << "\n";
        if (cardMode == CardMode::Shop && gameWorld) {
            gameWorld->clearClassicShopCards();
            gameWorld->setUnitDropZoneLayoutHint(0, false);
        }
        return;
    }

    const auto* viewport = services.viewport;
    const int uiW = viewport ? viewport->width : 1280;
    const int uiH = viewport ? viewport->height : 720;
    const bool useBackendCardUi = shouldUseBackendCardUi();
    clearBackendShopUiCache();

    if (cardMode == CardMode::Shop) {
        bool allItems = true;
        for (const auto& card : list) {
            if (card.type != CardType::Item) {
                allItems = false;
                break;
            }
        }
        if (gameWorld) {
            gameWorld->setClassicShopCards(game::state::shop_cards::toClassicCards(list));
            gameWorld->setUnitDropZoneLayoutHint(static_cast<int>(list.size()), allItems);
            gameWorld->setUnitSellRewardsEnabled(services.gameMode == "classic");
        }
        if (useBackendCardUi) {
            rebuildBackendCardUi(list, uiW, uiH, /*isItemRow=*/false);
            if (hasShopItems) {
                sol::protected_function itemFn = S["get_shop_items"];
                std::vector<CardData> items;
                std::string itemParseError;
                if (game::scripting::parseCardList(itemFn, items, &itemParseError)) {
                    rebuildBackendCardUi(items, uiW, uiH, /*isItemRow=*/true);
                } else {
                    std::cerr << "[ScriptedState] failed to parse item card list: " << itemParseError << "\n";
                    backendItemButtons.clear();
                    backendShopSnapshot.clear();
                    resetBackendShopActionRects();
                }
            }
            std::cout << "[ScriptedState] Spawned " << list.size() << " cards\n";
            return;
        }
        if (shopUi) {
            shopUi->setCards(list, uiW, uiH);
        }
        if (hasShopItems) {
            sol::protected_function itemFn = S["get_shop_items"];
            std::vector<CardData> items;
            std::string itemParseError;
            if (game::scripting::parseCardList(itemFn, items, &itemParseError)) {
                const game::ui::ShopRowLayout itemLayout = game::ui::computeShopRowLayout(uiW, uiH, /*allItems=*/true);
                const int itemW = itemLayout.cardW;
                const int itemH = itemLayout.cardH;
                const int itemSpacing = itemLayout.spacing;
                // Keep item shop row below header/ready UI to avoid overlap.
                const int headerClearanceY = std::max(96, static_cast<int>(std::round(static_cast<float>(uiH) * 0.14f)));
                const int itemY = std::max(headerClearanceY, itemLayout.edgeMargin);
                itemCardSystem.spawnCardRowLayout(items, uiW, itemY, itemW, itemH, itemSpacing);
            } else {
                std::cerr << "[ScriptedState] failed to parse item card list: " << itemParseError << "\n";
                itemCardSystem.clearCards();
            }
        } else {
            itemCardSystem.clearCards();
        }
    } else {
        if (gameWorld) {
            gameWorld->clearClassicShopCards();
            gameWorld->setUnitDropZoneLayoutHint(0, false);
        }
        if (useBackendCardUi) {
            rebuildBackendCardUi(list, uiW, uiH, /*isItemRow=*/false);
        } else {
            cardSystem.spawnCardRow(list, uiW, /*y*/ 300);
        }
    }
    std::cout << "[ScriptedState] Spawned " << list.size() << " cards\n";
}

void ScriptedState::ensureCardUI() {
    if (uiInitialized) return;
    sol::table S = script.getScriptTable();
    const ScriptedUiCapabilities caps = readScriptedUiCapabilities(S);
    hasShopItems = caps.hasShopItems;
    hasShopReadyButton = caps.hasShopReadyButton;
    hasShopRerollButton = caps.hasShopRerollButton;
    hasTextMenu = caps.hasTextMenu;
    renderWorld = caps.renderWorld;

    switch (chooseScriptedUiMode(caps)) {
        case ScriptedUiMode::TextMenu:
            cardMode = CardMode::TextMenu;
            break;
        case ScriptedUiMode::Shop:
            cardMode = CardMode::Shop;
            break;
        case ScriptedUiMode::Starter:
            cardMode = CardMode::Starter;
            break;
        default:
            cardMode = CardMode::None;
            hasShopItems = false;
            hasShopReadyButton = false;
            hasShopRerollButton = false;
            clearBackendShopUiCache();
            if (gameWorld) {
                gameWorld->clearClassicShopCards();
                gameWorld->setUnitDropZoneLayoutHint(0, false);
            }
            if (shopUi) shopUi->clear();
            uiInitialized = true;
            script.flushCommands();
            return;
    }

    if (!services.renderer) {
        clearBackendShopUiCache();
        if (cardMode == CardMode::TextMenu) {
            rebuildTextMenu();
            logHeadlessTextMenuHints();
        } else {
            rebuildCardRow();
        }
        uiInitialized = true;
        script.flushCommands();
        return;
    }

    const bool useBackendCardUi = shouldUseBackendCardUi();
    if (useBackendCardUi) {
        cardSystem.clearCards();
        itemCardSystem.clearCards();
        if (shopUi) shopUi->clear();
        titleText.reset();
        if (cardMode == CardMode::TextMenu) {
            rebuildTextMenu();
            logHeadlessTextMenuHints();
        } else {
            rebuildCardRow();
        }
        uiInitialized = true;
        script.flushCommands();
        return;
    }

    const auto& c = services.config;
    if (cardMode != CardMode::TextMenu) {
        if (cardMode == CardMode::Shop) {
            if (!shopUi) {
                shopUi = std::make_unique<game::ui::ShopUiFacade>();
                shopUi->init(c.fontPath, std::max(28, c.fontSize / 2), std::max(16, c.fontSize / 2));
            }
        } else {
            if (shopUi) shopUi->clear();
            cardSystem.init();
            cardSystem.initOverlayText(c.fontPath, std::max(16, c.fontSize / 2));
        }
        if (hasShopItems) {
            itemCardSystem.init();
            itemCardSystem.initOverlayText(c.fontPath, std::max(16, c.fontSize / 2));
        }
    } else {
        cardSystem.clearCards();
        itemCardSystem.clearCards();
        if (shopUi) shopUi->clear();
        hasShopReadyButton = false;
        hasShopRerollButton = false;
    }
    titleText = std::make_unique<TextRenderer>(c.fontPath, c.fontSize);

    if (cardMode == CardMode::TextMenu) {
        rebuildTextMenu();
    } else {
        rebuildCardRow();
    }

    uiInitialized = true;
    script.flushCommands();
}
