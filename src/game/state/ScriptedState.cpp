#include "ScriptedState.h"

#include "game/GameStateManager.h"
#include "game/GameServices.h"
#include "game/scripting/LuaCardParser.h"
#include "game/scripting/LuaScriptHelpers.h"
#include "game/scripting/LuaTextMenuParser.h"
#include "game/runtime/BackendCardRenderer.h"
#include "game/runtime/BackendDebugText.h"
#include "game/runtime/GameServiceRenderRoutes.h"
#include "game/runtime/BackendSellOverlayModel.h"
#include "game/runtime/BackendShopHudModel.h"
#include "game/runtime/BackendUiScale.h"
#include "game/state/BackendInputSlots.h"
#include "game/state/PlacementState.h"
#include "game/state/BackendUiPolicy.h"
#include "game/state/ShopCardConversion.h"
#include "game/ui/ShopLayout.h"
#include "game/ui/SellOverlayUiPolicy.h"
#include "game/ui/UIViewport.h"
#include "engine/input/InputEvent.h"
#include "engine/render/IRenderBackend.h"

#include <sol/sol.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
constexpr float kBackendTextScaleBase = 1.35f;

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

bool ScriptedState::shouldUseBackendCardUi() const {
    const auto routes = game::runtime::render::routesFromServices(services);
    return game::state::backend_ui::shouldUseBackendUi(
        routes);
}

void ScriptedState::resetBackendShopActionRects() {
    backendRerollX = 0.0f;
    backendRerollY = 0.0f;
    backendRerollW = 0.0f;
    backendRerollH = 0.0f;
    shopReadyX = 0.0f;
    shopReadyY = 0.0f;
    shopReadyW = 0.0f;
    shopReadyH = 0.0f;
}

void ScriptedState::clearBackendShopUiCache() {
    backendMainButtons.clear();
    backendItemButtons.clear();
    backendShopSnapshot.clear();
    resetBackendShopActionRects();
}

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

void ScriptedState::rebuildTextMenu() {
    textMenuEntries.clear();

    sol::table S = script.getScriptTable();
    sol::protected_function f = S["get_text_menu_entries"];
    if (!f.valid()) return;

    std::vector<game::scripting::TextMenuEntryData> parsed;
    std::string parseError;
    if (!game::scripting::parseTextMenuEntries(f, parsed, &parseError)) {
        std::cerr << "[ScriptedState] failed to parse text menu: " << parseError << "\n";
        return;
    }

    for (const auto& src : parsed) {
        TextMenuEntry entry;
        entry.id = src.id;
        entry.label = src.label;
        entry.scale = src.scale;
        entry.enabled = src.enabled;
        entry.bold = src.bold;
        entry.underline = src.underline;
        entry.hasCustomX = src.hasCustomX;
        entry.hasCustomY = src.hasCustomY;
        entry.xFrac = src.xFrac;
        entry.yFrac = src.yFrac;
        entry.anchorCenter = src.anchorCenter;
        entry.hasColor = src.hasColor;
        entry.colorR = src.colorR;
        entry.colorG = src.colorG;
        entry.colorB = src.colorB;
        textMenuEntries.push_back(std::move(entry));
    }

    const auto* viewport = services.viewport;
    const int uiW = viewport ? viewport->width : 1280;
    const int uiH = viewport ? viewport->height : 720;
    layoutBackendTextMenu(uiW, uiH);
}

void ScriptedState::layoutBackendTextMenu(int uiW, int uiH) {
    const auto applyLayout = [&](float scaleMul) -> float {
        const float autoStartY = std::max(110.0f, static_cast<float>(uiH) * 0.22f);
        const float rowGap = std::max(6.0f, static_cast<float>(uiH) * 0.016f) * scaleMul;
        float autoY = autoStartY;
        float maxBottom = 0.0f;

        int keyboardIndex = 0;
        for (auto& entry : textMenuEntries) {
            std::string display = entry.label;
            if (entry.enabled) {
                ++keyboardIndex;
                if (keyboardIndex <= 9) {
                    display = "[" + std::to_string(keyboardIndex) + "] " + display;
                }
            }

            const float textScale = std::max(0.1f, entry.scale) * kBackendTextScaleBase * scaleMul;
            entry.w = std::max(8.0f, game::runtime::backend_text::measureTextWidth(display, textScale));
            entry.h = std::max(8.0f, game::runtime::backend_text::measureTextHeight(display, textScale));

            if (entry.hasCustomX) {
                const float anchorX = static_cast<float>(uiW) * entry.xFrac;
                entry.x = entry.anchorCenter ? (anchorX - entry.w * 0.5f) : anchorX;
            } else {
                entry.x = (static_cast<float>(uiW) - entry.w) * 0.5f;
            }

            if (entry.hasCustomY) {
                entry.y = static_cast<float>(uiH) * entry.yFrac;
            } else {
                entry.y = autoY;
                autoY += entry.h + rowGap;
            }

            maxBottom = std::max(maxBottom, entry.y + entry.h);
        }
        return maxBottom;
    };

    backendTextMenuScale = 1.0f;
    float maxBottom = applyLayout(backendTextMenuScale);
    const float maxAllowed = static_cast<float>(uiH) - 18.0f;
    if (maxBottom > maxAllowed) {
        float minTop = static_cast<float>(uiH);
        for (const auto& entry : textMenuEntries) {
            minTop = std::min(minTop, entry.y);
        }
        const float span = std::max(1.0f, maxBottom - minTop);
        const float targetSpan = std::max(40.0f, maxAllowed - minTop);
        backendTextMenuScale = std::clamp(targetSpan / span, 0.55f, 1.0f);
        maxBottom = applyLayout(backendTextMenuScale);

        if (maxBottom > maxAllowed) {
            const float shiftUp = maxBottom - maxAllowed;
            for (auto& entry : textMenuEntries) {
                entry.y = std::max(8.0f, entry.y - shiftUp);
            }
        }
    }
}

void ScriptedState::rebuildBackendCardUi(const std::vector<CardData>& cards, int uiW, int uiH, bool isItemRow) {
    game::state::backend_cards::BuildInput in;
    in.cards = cards;
    in.uiW = uiW;
    in.uiH = uiH;
    in.mode = (cardMode == CardMode::Shop)
        ? game::state::backend_cards::LayoutMode::Shop
        : game::state::backend_cards::LayoutMode::Starter;
    in.forceItemRow = isItemRow;

    std::vector<game::state::backend_cards::Button>& out = isItemRow ? backendItemButtons : backendMainButtons;
    out = game::state::backend_cards::buildButtons(in);
}

void ScriptedState::refreshBackendShopSnapshot() {
    const bool isShopMode = (cardMode == CardMode::Shop);
    const bool hasWorld = (gameWorld != nullptr);
    const bool showSellOverlay = game::state::backend_ui::shouldShowSellOverlay(
        isShopMode,
        hasWorld,
        hasWorld && gameWorld->isUnitDragActive(),
        hasWorld ? gameWorld->getUnitDropZoneCardCount() : 0);
    const bool includeItemRow = isShopMode &&
        game::ui::sell_overlay::shouldRenderItemRow(hasShopItems, showSellOverlay);

    game::state::backend_shop::BuildInput input;
    input.shopMode = isShopMode;
    input.mainCount = backendMainButtons.size();
    input.itemCount = includeItemRow ? backendItemButtons.size() : 0u;
    input.includeItemRow = includeItemRow;
    input.includeReroll = isShopMode && hasShopRerollButton;
    input.includeReady = isShopMode && hasShopReadyButton;
    backendShopSnapshot = game::state::backend_shop::buildEntries(input);
    std::vector<game::state::backend_shop::Rect> mainRects;
    mainRects.reserve(backendMainButtons.size());
    for (const auto& button : backendMainButtons) {
        mainRects.push_back(game::state::backend_shop::Rect{
            button.x,
            button.y,
            button.w,
            button.h
        });
    }

    std::vector<game::state::backend_shop::Rect> itemRects;
    itemRects.reserve(backendItemButtons.size());
    for (const auto& button : backendItemButtons) {
        itemRects.push_back(game::state::backend_shop::Rect{
            button.x,
            button.y,
            button.w,
            button.h
        });
    }

    game::state::backend_shop::PlacementInput placement;
    placement.mainRects = &mainRects;
    placement.itemRects = includeItemRow ? &itemRects : nullptr;
    placement.rerollRect = game::state::backend_shop::Rect{
        backendRerollX,
        backendRerollY,
        backendRerollW,
        backendRerollH
    };
    placement.hasRerollRect = input.includeReroll;
    placement.readyRect = game::state::backend_shop::Rect{
        shopReadyX,
        shopReadyY,
        shopReadyW,
        shopReadyH
    };
    placement.hasReadyRect = input.includeReady;
    game::state::backend_shop::applyPlacement(backendShopSnapshot, placement);
}

bool ScriptedState::invokeBackendShopEntry(const game::state::backend_shop::Entry& entry) {
    sol::table S = script.getScriptTable();

    switch (entry.action) {
        case game::state::backend_shop::ActionType::ShopCard: {
            if (entry.sourceIndex >= backendMainButtons.size()) return false;
            const auto& card = backendMainButtons[entry.sourceIndex];
            sol::function onClick = game::scripting::resolveFunction(S, {"on_shop_card_click", "on_card_click", "onCardClick"});
            if (onClick.valid()) onClick(card.data.pokemonName, card.data.level);
            script.flushCommands();
            rebuildCardRow();
            return true;
        }
        case game::state::backend_shop::ActionType::StarterCard: {
            if (entry.sourceIndex >= backendMainButtons.size()) return false;
            const auto& card = backendMainButtons[entry.sourceIndex];
            sol::function onClick = game::scripting::resolveFunction(S, {"on_card_click", "onCardClick"});
            if (onClick.valid()) onClick(card.data.pokemonName);
            script.flushCommands();
            if (stateManager) {
                stateManager->pushState(std::make_unique<PlacementState>(
                    stateManager, gameWorld, services, card.data.pokemonName));
            }
            return true;
        }
        case game::state::backend_shop::ActionType::ItemCard: {
            if (entry.sourceIndex >= backendItemButtons.size()) return false;
            const auto& card = backendItemButtons[entry.sourceIndex];
            sol::function onItemClick = game::scripting::resolveFunction(S, {"on_shop_item_click"});
            if (onItemClick.valid()) onItemClick(card.data.pokemonName, card.data.cost);
            script.flushCommands();
            rebuildCardRow();
            return true;
        }
        case game::state::backend_shop::ActionType::ShopReroll: {
            sol::function onReroll = game::scripting::resolveFunction(S, {"on_shop_reroll_click"});
            if (onReroll.valid()) onReroll();
            script.flushCommands();
            rebuildCardRow();
            return true;
        }
        case game::state::backend_shop::ActionType::ShopReady: {
            sol::function onReady = game::scripting::resolveFunction(S, {"on_shop_ready_click"});
            if (onReady.valid()) onReady();
            script.flushCommands();
            rebuildCardRow();
            return true;
        }
    }

    return false;
}

void ScriptedState::renderBackendCardUi(int uiW, int uiH) {
    if (!services.renderer) return;
    if (cardMode != CardMode::Shop && cardMode != CardMode::Starter) return;
    const float uiScale = game::runtime::backend_ui::viewportScale(uiW, uiH);
    const float edgePad = game::runtime::backend_ui::edgePad(uiW, uiH);
    const float lineStep = game::runtime::backend_ui::lineStep(uiW, uiH);

    std::vector<IRenderBackend::DebugQuad> baseQuads;
    baseQuads.reserve(4096);
    std::vector<IRenderBackend::DebugLine> textLines;
    textLines.reserve(8192);
    std::vector<IRenderBackend::DebugSprite> sprites;
    sprites.reserve(1024);
    const bool isShopMode = (cardMode == CardMode::Shop);
    const bool hasWorld = (gameWorld != nullptr);
    const int dropZoneCardCount = hasWorld ? gameWorld->getUnitDropZoneCardCount() : 0;
    const bool useItemLayout = hasWorld ? gameWorld->getUnitDropZoneUsesItemLayout() : false;
    const bool showSellOverlay = game::state::backend_ui::shouldShowSellOverlay(
        isShopMode,
        hasWorld,
        hasWorld && gameWorld->isUnitDragActive(),
        dropZoneCardCount);

    refreshBackendShopSnapshot();

    const auto addButton = [&](float x,
                               float y,
                               const std::string& label,
                               float scale,
                               float r,
                               float g,
                               float b,
                               float* outW,
                               float* outH) {
        const float textScale = std::max(0.1f, scale) * kBackendTextScaleBase * uiScale;
        const float textW = std::max(1.0f, game::runtime::backend_text::measureTextWidth(label, textScale));
        const float textH = std::max(1.0f, game::runtime::backend_text::measureTextHeight(label, textScale));
        const float padX = std::max(8.0f, textScale * 4.0f);
        const float padY = std::max(5.0f, textScale * 2.5f);

        IRenderBackend::DebugQuad bg;
        bg.x = x - padX;
        bg.y = y - padY;
        bg.w = textW + padX * 2.0f;
        bg.h = textH + padY * 2.0f;
        bg.r = r;
        bg.g = g;
        bg.b = b;
        bg.a = 0.92f;
        baseQuads.push_back(bg);

        game::runtime::backend_text::appendTextLines(
            textLines, x, y, label, textScale, 0.98f, 0.98f, 0.98f, 1.0f, 0.88f);
        if (outW) *outW = bg.w;
        if (outH) *outH = bg.h;
    };
    const auto appendCenteredText = [&](float centerX,
                                        float y,
                                        const std::string& text,
                                        float scale,
                                        float r,
                                        float g,
                                        float b) {
        const float textScale = std::max(0.1f, scale) * kBackendTextScaleBase * uiScale;
        const float textW = std::max(1.0f, game::runtime::backend_text::measureTextWidth(text, textScale));
        const float x = centerX - textW * 0.5f;
        game::runtime::backend_text::appendTextLines(
            textLines, x, y, text, textScale, r, g, b, 1.0f, 0.88f);
    };

    const auto msgOpt = game::scripting::callStringFunction(script.getScriptTable(), {"get_message"});
    const std::string header = msgOpt ? *msgOpt : ((cardMode == CardMode::Starter) ? "Starter" : "Shop");
    game::runtime::backend_text::appendTextLines(
        textLines,
        edgePad,
        std::max(10.0f, edgePad - lineStep * 0.15f),
        header,
        std::clamp(2.6f * uiScale, 1.6f, 3.3f),
        0.95f,
        0.95f,
        0.98f,
        1.0f,
        0.88f);

    resetBackendShopActionRects();

    const auto addCardRow = [&](const std::vector<game::state::backend_cards::Button>& row,
                                game::state::backend_shop::ActionType action,
                                bool itemRow) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            const auto& card = row[i];
            const int slot = game::state::backend_shop::keyboardSlotFor(backendShopSnapshot, action, i);
            game::runtime::backend_card_renderer::CardRenderInput renderIn;
            renderIn.x = card.x;
            renderIn.y = card.y;
            renderIn.w = card.w;
            renderIn.h = card.h;
            renderIn.displayName = card.data.label;
            renderIn.speciesName = card.data.pokemonName;
            renderIn.subtitle = (card.data.level > 0)
                ? ("Lv" + std::to_string(card.data.level))
                : std::string();
            renderIn.explicitImagePath = card.data.imagePath;
            renderIn.u0 = card.data.uvMin.x;
            renderIn.v0 = card.data.uvMin.y;
            renderIn.u1 = card.data.uvMax.x;
            renderIn.v1 = card.data.uvMax.y;
            renderIn.keyboardSlot = slot;
            renderIn.item = itemRow || card.item;
            renderIn.textScale = std::clamp(1.0f * uiScale, 0.70f, 1.35f);
            renderIn.spriteAlpha = 1.0f;
            game::runtime::backend_card_renderer::appendCardLayered(
                baseQuads,
                nullptr,
                &sprites,
                renderIn,
                &textLines);
        }
    };

    addCardRow(
        backendMainButtons,
        isShopMode ? game::state::backend_shop::ActionType::ShopCard
                   : game::state::backend_shop::ActionType::StarterCard,
        /*itemRow=*/false);
    if (game::ui::sell_overlay::shouldRenderItemRow(hasShopItems, showSellOverlay)) {
        addCardRow(backendItemButtons, game::state::backend_shop::ActionType::ItemCard, /*itemRow=*/true);
    }

    const auto sellOverlay = game::runtime::backend_sell_overlay::buildModel(
        showSellOverlay && hasWorld,
        uiW,
        uiH,
        dropZoneCardCount,
        useItemLayout,
        hasWorld ? gameWorld->isUnitSellRewardsEnabled() : true);
    if (sellOverlay.visible) {
        const auto& outer = sellOverlay.outer;
        const auto& hit = sellOverlay.hit;
            IRenderBackend::DebugQuad outerBg;
            outerBg.x = static_cast<float>(outer.x);
            outerBg.y = static_cast<float>(outer.y);
            outerBg.w = static_cast<float>(outer.w);
            outerBg.h = static_cast<float>(outer.h);
            outerBg.r = 0.36f;
            outerBg.g = 0.07f;
            outerBg.b = 0.09f;
            outerBg.a = 0.82f;
            baseQuads.push_back(outerBg);

            if (hit.w > 0 && hit.h > 0) {
                IRenderBackend::DebugQuad hitBg;
                hitBg.x = static_cast<float>(hit.x);
                hitBg.y = static_cast<float>(hit.y);
                hitBg.w = static_cast<float>(hit.w);
                hitBg.h = static_cast<float>(hit.h);
                hitBg.r = 0.88f;
                hitBg.g = 0.21f;
                hitBg.b = 0.16f;
                hitBg.a = 0.90f;
                baseQuads.push_back(hitBg);
            }

            appendCenteredText(sellOverlay.centerX,
                               sellOverlay.titleY,
                               sellOverlay.copy.title,
                               sellOverlay.copy.titleScale,
                               0.99f,
                               0.95f,
                               0.90f);
            appendCenteredText(sellOverlay.centerX,
                               sellOverlay.hintY,
                               sellOverlay.copy.hint,
                               sellOverlay.copy.hintScale,
                               0.98f,
                               0.86f,
                               0.82f);
    }

    if (cardMode == CardMode::Shop) {
        const std::string moneyLabel = game::runtime::backend_shop_hud::moneyLabel(
            gameWorld ? gameWorld->getMoney() : 0);
        const int rerollSlot = game::state::backend_shop::keyboardSlotFor(
            backendShopSnapshot,
            game::state::backend_shop::ActionType::ShopReroll,
            0);
        const std::string rerollLabel = game::runtime::backend_shop_hud::rerollLabel(rerollSlot);

        int cardsX = 18;
        int cardsY = std::max(0, uiH - 120);
        int cardsH = 96;
        if (!backendMainButtons.empty()) {
            cardsX = game::runtime::backend_shop_hud::cardsAnchorX(backendMainButtons.front().x);
            cardsY = game::runtime::backend_shop_hud::cardsAnchorY(backendMainButtons.front().y, uiH);
            cardsH = game::runtime::backend_shop_hud::cardsAnchorH(backendMainButtons.front().h);
        }

        const float moneyScale = 1.0f * kBackendTextScaleBase * uiScale;
        const float rerollScale = 1.0f * kBackendTextScaleBase * uiScale;
        const float moneyW = game::runtime::backend_text::measureTextWidth(moneyLabel, moneyScale);
        const float moneyH = game::runtime::backend_text::measureTextHeight(moneyLabel, moneyScale);
        const float rerollW = game::runtime::backend_text::measureTextWidth(rerollLabel, rerollScale);
        const float rerollH = game::runtime::backend_text::measureTextHeight(rerollLabel, rerollScale);

        game::runtime::backend_shop_hud::LayoutInput hudIn;
        hudIn.uiW = uiW;
        hudIn.uiH = uiH;
        hudIn.cardsX = cardsX;
        hudIn.cardsY = cardsY;
        hudIn.cardsH = cardsH;
        hudIn.moneyTextW = moneyW;
        hudIn.moneyTextH = moneyH;
        hudIn.rerollTextW = rerollW;
        hudIn.rerollTextH = rerollH;
        hudIn.showReroll = hasShopRerollButton;
        const game::ui::ClassicHudLayout hud = game::runtime::backend_shop_hud::computeLayout(hudIn);

        game::runtime::backend_text::appendTextLines(
            textLines, hud.textX, hud.textY, moneyLabel, moneyScale, 0.95f, 0.88f, 0.50f, 1.0f, 0.88f);

        if (hasShopRerollButton) {
            const float buttonTextX = hud.rerollX;
            const float buttonTextY = hud.rerollY;
            float buttonW = 0.0f;
            float buttonH = 0.0f;
            addButton(buttonTextX, buttonTextY, rerollLabel, 1.0f,
                      0.20f, 0.16f, 0.08f, &buttonW, &buttonH);
            backendRerollX = buttonTextX - std::max(8.0f, kBackendTextScaleBase * 4.0f * uiScale);
            backendRerollY = buttonTextY - std::max(5.0f, kBackendTextScaleBase * 2.5f * uiScale);
            backendRerollW = buttonW;
            backendRerollH = buttonH;
        }
        if (hasShopReadyButton) {
            const int readySlot = game::state::backend_shop::keyboardSlotFor(
                backendShopSnapshot,
                game::state::backend_shop::ActionType::ShopReady,
                0);
            const std::string readyLabel = game::runtime::backend_shop_hud::keyboardPrefixedLabel(readySlot, "Ready");
            const float textScale = 1.0f * kBackendTextScaleBase * uiScale;
            const float textW = std::max(1.0f, game::runtime::backend_text::measureTextWidth(readyLabel, textScale));
            const float padX = std::max(8.0f, textScale * 4.0f);
            const float padY = std::max(5.0f, textScale * 2.5f);
            const float textX = static_cast<float>(uiW) - textW - padX * 2.0f - edgePad + padX;
            const float textY = edgePad + lineStep * 0.95f;
            float buttonW = 0.0f;
            float buttonH = 0.0f;
            addButton(textX, textY, readyLabel, 1.0f, 0.12f, 0.25f, 0.14f, &buttonW, &buttonH);
            shopReadyX = textX - padX;
            shopReadyY = textY - padY;
            shopReadyW = buttonW;
            shopReadyH = buttonH;
        }
    }

    // Rebuild once after reroll/ready rects are known so mouse hit-testing stays in sync.
    refreshBackendShopSnapshot();

    game::runtime::backend_text::appendTextLines(
        textLines,
        edgePad,
        std::max(4.0f, static_cast<float>(uiH) - edgePad - lineStep * 0.8f),
        game::runtime::backend_shop_hud::interactionHint(),
        std::clamp(1.0f * uiScale, 0.80f, 1.30f),
        0.72f,
        0.82f,
        0.93f,
        1.0f,
        0.88f);

    if (!baseQuads.empty()) {
        services.renderer->drawDebugQuads(baseQuads.data(), baseQuads.size(), uiW, uiH);
    }
    if (!sprites.empty()) {
        services.renderer->drawDebugSprites(sprites.data(), sprites.size(), uiW, uiH);
    }
    if (!textLines.empty()) {
        services.renderer->drawDebugLines(textLines.data(), textLines.size(), uiW, uiH);
    }
}

bool ScriptedState::tryHandleBackendCardKey(InputEvent::Key keyId) {
    const int target = game::state::backend_input::slotFromNumberKey(keyId);

    if (target <= 0) return false;
    refreshBackendShopSnapshot();
    const auto* entry = game::state::backend_shop::findByKeyboardSlot(backendShopSnapshot, target);
    if (!entry) return false;
    return invokeBackendShopEntry(*entry);
}

bool ScriptedState::handleBackendCardMouseClick(int mouseX, int mouseY) {
    if (cardMode != CardMode::Shop && cardMode != CardMode::Starter) return false;

    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);
    refreshBackendShopSnapshot();
    const auto* entry = game::state::backend_shop::findByPoint(backendShopSnapshot, mx, my);
    if (!entry) return false;
    return invokeBackendShopEntry(*entry);
}

void ScriptedState::renderBackendTextMenu(int uiW, int uiH) {
    if (!services.renderer || cardMode != CardMode::TextMenu) return;

    layoutBackendTextMenu(uiW, uiH);

    std::vector<IRenderBackend::DebugQuad> baseQuads;
    baseQuads.reserve(1024);
    std::vector<IRenderBackend::DebugLine> textLines;
    textLines.reserve(8192);

    const auto appendOutline = [&](float x,
                                   float y,
                                   float w,
                                   float h,
                                   float thickness,
                                   float r,
                                   float g,
                                   float b,
                                   float a) {
        if (w <= 0.0f || h <= 0.0f || thickness <= 0.0f) return;
        IRenderBackend::DebugQuad top;
        top.x = x;
        top.y = y;
        top.w = w;
        top.h = thickness;
        top.r = r;
        top.g = g;
        top.b = b;
        top.a = a;
        baseQuads.push_back(top);

        IRenderBackend::DebugQuad bottom = top;
        bottom.y = y + std::max(0.0f, h - thickness);
        baseQuads.push_back(bottom);

        IRenderBackend::DebugQuad left;
        left.x = x;
        left.y = y + thickness;
        left.w = thickness;
        left.h = std::max(0.0f, h - thickness * 2.0f);
        left.r = r;
        left.g = g;
        left.b = b;
        left.a = a;
        baseQuads.push_back(left);

        IRenderBackend::DebugQuad right = left;
        right.x = x + std::max(0.0f, w - thickness);
        baseQuads.push_back(right);
    };

    IRenderBackend::DebugQuad screenBackdrop;
    screenBackdrop.x = 0.0f;
    screenBackdrop.y = 0.0f;
    screenBackdrop.w = static_cast<float>(uiW);
    screenBackdrop.h = static_cast<float>(uiH);
    screenBackdrop.r = 0.02f;
    screenBackdrop.g = 0.04f;
    screenBackdrop.b = 0.08f;
    screenBackdrop.a = 0.46f;
    baseQuads.push_back(screenBackdrop);

    float minX = static_cast<float>(uiW);
    float minY = static_cast<float>(uiH);
    float maxX = 0.0f;
    float maxY = 0.0f;
    bool hasAnyEntry = false;

    int keyboardIndex = 0;
    for (const auto& entry : textMenuEntries) {
        std::string display = entry.label;
        if (entry.enabled) {
            ++keyboardIndex;
            if (keyboardIndex <= 9) {
                display = "[" + std::to_string(keyboardIndex) + "] " + display;
            }
        }

        const float textScale = std::max(0.1f, entry.scale) * kBackendTextScaleBase * backendTextMenuScale;
        const float padX = std::max(8.0f, 10.0f * textScale * 0.5f);
        const float padY = std::max(4.0f, 6.0f * textScale * 0.5f);
        const float border = std::clamp(0.9f + textScale * 0.08f, 1.0f, 2.4f);

        IRenderBackend::DebugQuad bg;
        bg.x = entry.x - padX;
        bg.y = entry.y - padY;
        bg.w = entry.w + padX * 2.0f;
        bg.h = entry.h + padY * 2.0f;
        if (!entry.enabled) {
            bg.r = 0.16f;
            bg.g = 0.16f;
            bg.b = 0.18f;
            bg.a = 0.85f;
        } else if (entry.hasColor) {
            bg.r = std::clamp(entry.colorR * 0.24f, 0.0f, 1.0f);
            bg.g = std::clamp(entry.colorG * 0.24f, 0.0f, 1.0f);
            bg.b = std::clamp(entry.colorB * 0.24f, 0.0f, 1.0f);
            bg.a = 0.92f;
        } else {
            bg.r = 0.20f;
            bg.g = 0.22f;
            bg.b = 0.28f;
            bg.a = 0.92f;
        }
        baseQuads.push_back(bg);
        if (!entry.enabled) {
            appendOutline(bg.x, bg.y, bg.w, bg.h, border, 0.36f, 0.38f, 0.42f, 0.96f);
        } else if (entry.hasColor) {
            appendOutline(bg.x,
                          bg.y,
                          bg.w,
                          bg.h,
                          border,
                          std::clamp(entry.colorR * 0.95f, 0.0f, 1.0f),
                          std::clamp(entry.colorG * 0.95f, 0.0f, 1.0f),
                          std::clamp(entry.colorB * 0.95f, 0.0f, 1.0f),
                          0.98f);
        } else {
            appendOutline(bg.x, bg.y, bg.w, bg.h, border, 0.78f, 0.80f, 0.88f, 0.95f);
        }

        float tr = 1.0f;
        float tg = 1.0f;
        float tb = 1.0f;
        if (!entry.enabled) {
            tr = 0.55f;
            tg = 0.58f;
            tb = 0.62f;
        } else if (entry.hasColor) {
            tr = entry.colorR;
            tg = entry.colorG;
            tb = entry.colorB;
        }

        if (entry.bold) {
            game::runtime::backend_text::appendTextLines(
                textLines,
                entry.x + 0.9f,
                entry.y + 0.9f,
                display,
                textScale,
                std::clamp(tr * 0.50f, 0.0f, 1.0f),
                std::clamp(tg * 0.50f, 0.0f, 1.0f),
                std::clamp(tb * 0.50f, 0.0f, 1.0f),
                0.72f,
                0.82f);
        }
        game::runtime::backend_text::appendTextLines(
            textLines, entry.x, entry.y, display, textScale, tr, tg, tb, 1.0f, 0.82f);
        if (entry.underline) {
            IRenderBackend::DebugLine ul;
            ul.x1 = entry.x;
            ul.x2 = entry.x + entry.w;
            ul.y1 = entry.y + entry.h * 0.78f;
            ul.y2 = ul.y1;
            ul.thickness = std::clamp(1.0f + textScale * 0.08f, 1.0f, 2.2f);
            ul.r = std::clamp(tr * 0.95f, 0.0f, 1.0f);
            ul.g = std::clamp(tg * 0.95f, 0.0f, 1.0f);
            ul.b = std::clamp(tb * 0.95f, 0.0f, 1.0f);
            ul.a = entry.enabled ? 0.96f : 0.72f;
            textLines.push_back(ul);
        }

        minX = std::min(minX, bg.x);
        minY = std::min(minY, bg.y);
        maxX = std::max(maxX, bg.x + bg.w);
        maxY = std::max(maxY, bg.y + bg.h);
        hasAnyEntry = true;
    }

    if (hasAnyEntry) {
        const auto msgOpt = game::scripting::callStringFunction(script.getScriptTable(), {"get_message"});
        std::string header = msgOpt ? *msgOpt : "Menu";
        game::runtime::backend_text::appendTextLines(textLines,
                                minX,
                                std::max(24.0f, minY - 62.0f),
                                header,
                                3.0f * backendTextMenuScale,
                                0.97f,
                                0.97f,
                                0.98f,
                                1.0f,
                                0.96f);
        game::runtime::backend_text::appendTextLines(textLines,
                                minX,
                                std::max(52.0f, minY - 30.0f),
                                "Click entries or press 1-9",
                                1.5f * backendTextMenuScale,
                                0.72f,
                                0.84f,
                                0.96f,
                                1.0f,
                                0.86f);

        IRenderBackend::DebugQuad panel;
        panel.x = std::max(8.0f, minX - 18.0f);
        panel.y = std::max(8.0f, minY - 20.0f);
        panel.w = std::min(static_cast<float>(uiW) - panel.x - 8.0f, (maxX - minX) + 36.0f);
        panel.h = std::min(static_cast<float>(uiH) - panel.y - 8.0f, (maxY - minY) + 28.0f);
        panel.r = 0.05f;
        panel.g = 0.06f;
        panel.b = 0.08f;
        panel.a = 0.70f;
        baseQuads.insert(baseQuads.begin() + 1, panel);
        const float panelBorder = std::clamp(1.2f + backendTextMenuScale * 0.8f, 1.4f, 2.8f);
        appendOutline(panel.x, panel.y, panel.w, panel.h, panelBorder, 0.42f, 0.54f, 0.70f, 0.96f);
    }

    if (!baseQuads.empty()) {
        services.renderer->drawDebugQuads(baseQuads.data(), baseQuads.size(), uiW, uiH);
    }
    if (!textLines.empty()) {
        services.renderer->drawDebugLines(textLines.data(), textLines.size(), uiW, uiH);
    }
}

void ScriptedState::logHeadlessTextMenuHints() const {
    if (!hasTextMenu || cardMode != CardMode::TextMenu) return;

    int option = 0;
    bool any = false;
    std::cout << "[Menu][BackendUI] Shared backend menu path active.\n";
    std::cout << "[Menu][BackendUI] Click the game window to focus, then use mouse or press 1-9:\n";
    for (const auto& entry : textMenuEntries) {
        if (!entry.enabled) continue;
        ++option;
        any = true;
        if (option <= 9) {
            std::cout << "  [" << option << "] " << entry.label << " (" << entry.id << ")\n";
        }
    }
    if (!any) {
        std::cout << "  (no selectable entries)\n";
    } else if (option > 9) {
        std::cout << "  [BackendUI] Only options 1-9 are keyboard-selectable.\n";
    }
}

bool ScriptedState::tryHandleHeadlessTextMenuKey(InputEvent::Key keyId) {
    const int targetOption = game::state::backend_input::slotFromNumberKey(keyId);
    if (targetOption <= 0) return false;

    int option = 0;
    for (const auto& entry : textMenuEntries) {
        if (!entry.enabled) continue;
        ++option;
        if (option != targetOption) continue;

        sol::table S = script.getScriptTable();
        sol::function onMenuClick = game::scripting::resolveFunction(S, {"on_text_menu_click", "on_menu_click"});
        if (!onMenuClick.valid()) {
            std::cout << "[Menu][BackendUI] Menu click handler unavailable.\n";
            return false;
        }
        std::cout << "[Menu][BackendUI] Selected [" << targetOption << "] " << entry.label << "\n";
        onMenuClick(entry.id);
        script.flushCommands();
        rebuildTextMenu();
        logHeadlessTextMenuHints();
        return true;
    }

    std::cout << "[Menu][BackendUI] No menu option mapped to key " << targetOption << ".\n";
    return false;
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

void ScriptedState::onEnter() {
    script.onEnter();
    ensureCardUI();
}

void ScriptedState::onExit() {
    clearBackendShopUiCache();
    hasShopReadyButton = false;
    hasShopRerollButton = false;
    if (gameWorld) {
        gameWorld->clearClassicShopCards();
        gameWorld->setUnitDropZoneLayoutHint(0, false);
    }
    script.onExit();
}

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
    if (event.type == InputEvent::Type::MouseDown && gameWorld) {
        if (gameWorld->consumeUiClickBlocked()) return;
        if (gameWorld->isUnitDragActive()) return;
    }

    sol::table S = script.getScriptTable();

    if (event.type == InputEvent::Type::MouseDown) {
        if (cardMode == CardMode::TextMenu) {
            for (const auto& entry : textMenuEntries) {
                if (!entry.enabled) continue;
                const bool insideX = static_cast<float>(event.mouseX) >= entry.x &&
                                     static_cast<float>(event.mouseX) <= (entry.x + entry.w);
                const bool insideY = static_cast<float>(event.mouseY) >= entry.y &&
                                     static_cast<float>(event.mouseY) <= (entry.y + entry.h);
                if (!(insideX && insideY)) continue;

                sol::function onMenuClick = game::scripting::resolveFunction(S, {"on_text_menu_click", "on_menu_click"});
                if (onMenuClick.valid()) {
                    onMenuClick(entry.id);
                }
                script.flushCommands();
                rebuildTextMenu();
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
                sol::function onClick = game::scripting::resolveFunction(S, {"on_card_click", "onCardClick"});
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

                        sol::function onClick = game::scripting::resolveFunction(S, {"on_card_click", "onCardClick"});
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
    const auto routes = game::runtime::render::routesFromServices(services);
    const bool renderBackendTextMenuPath = game::state::backend_ui::shouldRenderBackendTextMenu(
        routes,
        cardMode == CardMode::TextMenu);

    if (titleText && !renderBackendTextMenuPath) {
        const auto msgOpt = game::scripting::callStringFunction(S, {"get_message"});
        if (msgOpt) {
            const std::string& msg = *msgOpt;
            float w = titleText->measureTextWidth(msg, 1.0f);
            float x = (static_cast<float>(uiW) - w) * 0.5f;
            constexpr float kHeaderY = 58.0f;
            const glm::vec3 msgColor = (cardMode == CardMode::TextMenu)
                ? glm::vec3(1.0f, 1.0f, 1.0f)
                : glm::vec3(1.0f, 1.0f, 0.0f);
            titleText->renderText(msg, x, kHeaderY, msgColor, 1.0f);
        }
    }

    if (cardMode == CardMode::Shop && hasShopReadyButton && titleText) {
        const std::string readyLabel = "[ Ready ]";
        const float readyScale = 0.95f;
        shopReadyW = titleText->measureTextWidth(readyLabel, readyScale);
        shopReadyH = static_cast<float>(services.config.fontSize) * readyScale;
        shopReadyX = static_cast<float>(uiW) - shopReadyW - 36.0f;
        shopReadyY = 62.0f;
        titleText->renderText(readyLabel, shopReadyX, shopReadyY,
                              glm::vec3(1.0f, 1.0f, 1.0f), readyScale);
    } else {
        shopReadyW = 0.0f;
        shopReadyH = 0.0f;
    }

    if (renderBackendTextMenuPath) {
        renderBackendTextMenu(uiW, uiH);
        return;
    }

    if (cardMode == CardMode::TextMenu && titleText) {
        float autoY = 220.0f;
        for (auto& entry : textMenuEntries) {
            const float scale = std::max(0.1f, entry.scale);
            const float textH = static_cast<float>(services.config.fontSize) * scale;
            entry.w = titleText->measureTextWidth(entry.label, scale);
            entry.h = textH;

            if (entry.hasCustomX) {
                const float anchorX = static_cast<float>(uiW) * entry.xFrac;
                entry.x = entry.anchorCenter ? (anchorX - entry.w * 0.5f) : anchorX;
            } else {
                entry.x = (static_cast<float>(uiW) - entry.w) * 0.5f;
            }

            if (entry.hasCustomY) {
                entry.y = static_cast<float>(uiH) * entry.yFrac;
            } else {
                entry.y = autoY;
                autoY += textH + 16.0f;
            }

            glm::vec3 color(1.0f, 1.0f, 1.0f);
            if (!entry.enabled) {
                color = glm::vec3(0.55f, 0.55f, 0.60f);
            } else if (entry.hasColor) {
                color = glm::vec3(entry.colorR, entry.colorG, entry.colorB);
            }

            if (entry.bold) {
                titleText->renderText(entry.label, entry.x + 0.75f, entry.y, color, scale);
            }
            titleText->renderText(entry.label, entry.x, entry.y, color, scale);

            if (entry.underline) {
                const int underCount = std::max(4, static_cast<int>(std::round(entry.w / std::max(4.0f, 10.0f * scale))));
                const std::string under(static_cast<size_t>(underCount), '_');
                titleText->renderText(under, entry.x, entry.y + textH * 0.68f, color, scale);
            }
        }
        return;
    }

    if (shouldUseBackendCardUi() &&
        (cardMode == CardMode::Shop || cardMode == CardMode::Starter)) {
        renderBackendCardUi(uiW, uiH);
        return;
    }

    const bool showSellOverlay = game::state::backend_ui::shouldShowSellOverlay(
        cardMode == CardMode::Shop,
        gameWorld != nullptr,
        gameWorld && gameWorld->isUnitDragActive(),
        gameWorld ? gameWorld->getUnitDropZoneCardCount() : 0);

    if (cardMode == CardMode::Shop) {
        if (hasShopItems && !showSellOverlay) {
            itemCardSystem.render(uiW, uiH);
        }
        drawShopHud(uiW, uiH);
    } else {
        cardSystem.render(uiW, uiH);
    }
}

void ScriptedState::drawShopHud(int uiW, int uiH) {
    if (!shopUi || !shopUi->hasCards()) return;

    game::ui::ShopUiRenderInput in;
    in.uiW = uiW;
    in.uiH = uiH;
    in.money = gameWorld ? gameWorld->getMoney() : 0;
    in.showReroll = hasShopRerollButton;
    in.gameMode = services.gameMode;
    in.moneyScale = 1.35f;
    in.rerollScale = 0.90f;
    in.rerollLabel = "[Reroll 2g]";
    in.showSellOverlay = game::state::backend_ui::shouldShowSellOverlay(
        cardMode == CardMode::Shop,
        gameWorld != nullptr,
        gameWorld && gameWorld->isUnitDragActive(),
        gameWorld ? gameWorld->getUnitDropZoneCardCount() : 0);
    in.sellOverlayPaysMoney = gameWorld ? gameWorld->isUnitSellRewardsEnabled() : true;
    shopUi->render(in);
}
