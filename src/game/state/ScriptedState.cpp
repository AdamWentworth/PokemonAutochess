#include "ScriptedState.h"

#include "game/GameStateManager.h"
#include "game/GameServices.h"
#include "game/scripting/LuaCardParser.h"
#include "game/scripting/LuaScriptHelpers.h"
#include "game/scripting/LuaTextMenuParser.h"
#include "game/runtime/BackendDebugText.h"
#include "game/state/PlacementState.h"
#include "game/state/BackendUiPolicy.h"
#include "game/ui/ShopLayout.h"
#include "game/ui/SellOverlayUiPolicy.h"
#include "game/ui/UIViewport.h"
#include "engine/input/InputEvent.h"
#include "engine/render/IRenderBackend.h"

#include <sol/sol.hpp>
#include <stb_easy_font.h>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
constexpr float kBackendTextScaleBase = 1.35f;

std::vector<GameWorld::ClassicShopCard> buildClassicCardsFromUi(const std::vector<CardData>& cards) {
    std::vector<GameWorld::ClassicShopCard> out;
    out.reserve(cards.size());
    for (const auto& c : cards) {
        if (c.pokemonName.empty()) continue;
        if (c.type == CardType::Item) continue;
        GameWorld::ClassicShopCard card;
        card.name = c.pokemonName;
        card.level = std::max(1, c.level);
        card.cost = std::max(0, c.cost);
        out.push_back(std::move(card));
    }
    return out;
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
    return (services.renderer != nullptr) && (services.activeRendererBackend != "opengl");
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
    backendMainButtons.clear();
    backendItemButtons.clear();
    backendShopSnapshot.clear();
    backendRerollX = 0.0f;
    backendRerollY = 0.0f;
    backendRerollW = 0.0f;
    backendRerollH = 0.0f;

    if (cardMode == CardMode::Shop) {
        bool allItems = true;
        for (const auto& card : list) {
            if (card.type != CardType::Item) {
                allItems = false;
                break;
            }
        }
        if (gameWorld) {
            gameWorld->setClassicShopCards(buildClassicCardsFromUi(list));
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
            const int baseW = stb_easy_font_width(const_cast<char*>(display.c_str()));
            const int baseH = stb_easy_font_height(const_cast<char*>(display.c_str()));
            entry.w = std::max(8.0f, static_cast<float>(baseW) * textScale);
            entry.h = std::max(8.0f, static_cast<float>(baseH) * textScale);

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
    std::vector<BackendCardButton>& out = isItemRow ? backendItemButtons : backendMainButtons;
    out.clear();

    if (cards.empty()) return;

    bool allItems = isItemRow;
    if (!allItems) {
        allItems = true;
        for (const auto& card : cards) {
            if (card.type != CardType::Item) {
                allItems = false;
                break;
            }
        }
    }

    const game::ui::ShopRowLayout layout = game::ui::computeShopRowLayout(uiW, uiH, allItems);
    const int count = static_cast<int>(cards.size());
    const int cardW = std::max(64, layout.cardW);
    const int cardH = std::max(48, layout.cardH);
    const int spacing = std::max(6, layout.spacing);

    int startX = layout.edgeMargin;
    int rowY = layout.edgeMargin;
    if (cardMode == CardMode::Shop) {
        const game::ui::ShopRowPlacement place = game::ui::computeShopRowPlacement(uiW, uiH, count, layout);
        startX = place.startX;
        rowY = place.y;
        if (isItemRow) {
            const int topShelf = std::max(layout.edgeMargin + 64,
                                          static_cast<int>(std::round(static_cast<float>(uiH) * 0.16f)));
            rowY = topShelf;
        }
    } else {
        const int totalW = count * (cardW + spacing) - spacing;
        startX = std::max(layout.edgeMargin, (uiW - totalW) / 2);
        rowY = std::max(layout.edgeMargin + 72,
                        static_cast<int>(std::round(static_cast<float>(uiH) * 0.44f)));
    }

    out.reserve(cards.size());
    for (int i = 0; i < count; ++i) {
        BackendCardButton b;
        b.data = cards[static_cast<std::size_t>(i)];
        b.x = static_cast<float>(startX + i * (cardW + spacing));
        b.y = static_cast<float>(rowY);
        b.w = static_cast<float>(cardW);
        b.h = static_cast<float>(cardH);
        b.item = (b.data.type == CardType::Item) || isItemRow;
        out.push_back(std::move(b));
    }
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

    for (auto& entry : backendShopSnapshot) {
        switch (entry.action) {
            case game::state::backend_shop::ActionType::ShopCard:
            case game::state::backend_shop::ActionType::StarterCard: {
                if (entry.sourceIndex >= backendMainButtons.size()) break;
                const auto& button = backendMainButtons[entry.sourceIndex];
                entry.x = button.x;
                entry.y = button.y;
                entry.w = button.w;
                entry.h = button.h;
                break;
            }
            case game::state::backend_shop::ActionType::ItemCard: {
                if (entry.sourceIndex >= backendItemButtons.size()) break;
                const auto& button = backendItemButtons[entry.sourceIndex];
                entry.x = button.x;
                entry.y = button.y;
                entry.w = button.w;
                entry.h = button.h;
                break;
            }
            case game::state::backend_shop::ActionType::ShopReroll: {
                entry.x = backendRerollX;
                entry.y = backendRerollY;
                entry.w = backendRerollW;
                entry.h = backendRerollH;
                break;
            }
            case game::state::backend_shop::ActionType::ShopReady: {
                entry.x = shopReadyX;
                entry.y = shopReadyY;
                entry.w = shopReadyW;
                entry.h = shopReadyH;
                break;
            }
        }
    }
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

    std::vector<IRenderBackend::DebugQuad> quads;
    quads.reserve(4096);
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
        const float textScale = std::max(0.1f, scale) * kBackendTextScaleBase;
        const int baseW = stb_easy_font_width(const_cast<char*>(label.c_str()));
        const int baseH = stb_easy_font_height(const_cast<char*>(label.c_str()));
        const float textW = std::max(1.0f, static_cast<float>(baseW) * textScale);
        const float textH = std::max(1.0f, static_cast<float>(baseH) * textScale);
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
        quads.push_back(bg);

        game::runtime::backend_text::appendTextQuads(
            quads, x, y, label, textScale, 0.98f, 0.98f, 0.98f, 1.0f);
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
        const float textScale = std::max(0.1f, scale) * kBackendTextScaleBase;
        const int baseW = stb_easy_font_width(const_cast<char*>(text.c_str()));
        const float textW = std::max(1.0f, static_cast<float>(baseW) * textScale);
        const float x = centerX - textW * 0.5f;
        game::runtime::backend_text::appendTextQuads(
            quads, x, y, text, textScale, r, g, b, 1.0f);
    };

    const auto msgOpt = game::scripting::callStringFunction(script.getScriptTable(), {"get_message"});
    const std::string header = msgOpt ? *msgOpt : ((cardMode == CardMode::Starter) ? "Starter" : "Shop");
    game::runtime::backend_text::appendTextQuads(
        quads, 26.0f, 24.0f, header, 2.6f, 0.95f, 0.95f, 0.98f, 1.0f);

    backendRerollX = 0.0f;
    backendRerollY = 0.0f;
    backendRerollW = 0.0f;
    backendRerollH = 0.0f;
    shopReadyX = 0.0f;
    shopReadyY = 0.0f;
    shopReadyW = 0.0f;
    shopReadyH = 0.0f;

    const auto prefixedLabel = [&](int slot, const std::string& label) {
        if (slot > 0 && slot <= 9) {
            return "[" + std::to_string(slot) + "] " + label;
        }
        return label;
    };

    const auto addCardRow = [&](const std::vector<BackendCardButton>& row,
                                game::state::backend_shop::ActionType action,
                                bool itemRow) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            const auto& card = row[i];
            IRenderBackend::DebugQuad panel;
            panel.x = card.x;
            panel.y = card.y;
            panel.w = card.w;
            panel.h = card.h;
            if (itemRow || card.item) {
                panel.r = 0.26f;
                panel.g = 0.20f;
                panel.b = 0.10f;
            } else {
                panel.r = 0.14f;
                panel.g = 0.20f;
                panel.b = 0.28f;
            }
            panel.a = 0.92f;
            quads.push_back(panel);

            IRenderBackend::DebugQuad border;
            border.x = card.x + 1.0f;
            border.y = card.y + 1.0f;
            border.w = std::max(0.0f, card.w - 2.0f);
            border.h = std::max(0.0f, card.h - 2.0f);
            border.r = 0.06f;
            border.g = 0.07f;
            border.b = 0.10f;
            border.a = 0.40f;
            quads.push_back(border);

            const std::string name = card.data.label.empty() ? card.data.pokemonName : card.data.label;
            const int slot = game::state::backend_shop::keyboardSlotFor(backendShopSnapshot, action, i);
            const std::string indexed = prefixedLabel(slot, name);
            game::runtime::backend_text::appendTextQuads(quads,
                                    card.x + 8.0f,
                                    card.y + 8.0f,
                                    indexed,
                                    1.0f,
                                    0.97f,
                                    0.97f,
                                    0.99f,
                                    1.0f);

            std::string sub = "Lv " + std::to_string(std::max(1, card.data.level));
            if (cardMode == CardMode::Shop) {
                sub += "  Cost " + std::to_string(std::max(0, card.data.cost)) + "g";
            }
            game::runtime::backend_text::appendTextQuads(quads,
                                    card.x + 8.0f,
                                    card.y + std::max(16.0f, card.h - 24.0f),
                                    sub,
                                    0.9f,
                                    0.83f,
                                    0.90f,
                                    0.96f,
                                    1.0f);
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

    if (showSellOverlay && hasWorld) {
        const game::ui::SellDropZoneLayout outer = game::state::backend_ui::computeSellOverlayOuterLayout(
            uiW,
            uiH,
            dropZoneCardCount,
            useItemLayout);
        const game::ui::SellDropZoneLayout hit = game::state::backend_ui::computeSellOverlayHitLayout(outer);
        if (outer.w > 0 && outer.h > 0) {
            IRenderBackend::DebugQuad outerBg;
            outerBg.x = static_cast<float>(outer.x);
            outerBg.y = static_cast<float>(outer.y);
            outerBg.w = static_cast<float>(outer.w);
            outerBg.h = static_cast<float>(outer.h);
            outerBg.r = 0.36f;
            outerBg.g = 0.07f;
            outerBg.b = 0.09f;
            outerBg.a = 0.82f;
            quads.push_back(outerBg);

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
                quads.push_back(hitBg);
            }

            const float cx = static_cast<float>(outer.x) + static_cast<float>(outer.w) * 0.5f;
            const float titleY = static_cast<float>(outer.y) + 10.0f;
            const float hintY = static_cast<float>(outer.y) + static_cast<float>(outer.h) * 0.58f;
            const bool pays = gameWorld->isUnitSellRewardsEnabled();
            const game::ui::sell_overlay::Copy copy = game::ui::sell_overlay::makeCopy(pays);
            appendCenteredText(cx,
                               titleY,
                               copy.title,
                               copy.titleScale,
                               0.99f,
                               0.95f,
                               0.90f);
            appendCenteredText(cx,
                               hintY,
                               copy.hint,
                               copy.hintScale,
                               0.98f,
                               0.86f,
                               0.82f);
        }
    }

    if (cardMode == CardMode::Shop) {
        if (gameWorld) {
            const std::string moneyLabel = "Gold: " + std::to_string(std::max(0, gameWorld->getMoney()));
            game::runtime::backend_text::appendTextQuads(
                quads, 26.0f, 64.0f, moneyLabel, 1.3f, 0.95f, 0.88f, 0.50f, 1.0f);
        }
        if (hasShopRerollButton) {
            const float buttonTextX = 26.0f;
            const float buttonTextY = 96.0f;
            float buttonW = 0.0f;
            float buttonH = 0.0f;
            const int rerollSlot = game::state::backend_shop::keyboardSlotFor(
                backendShopSnapshot,
                game::state::backend_shop::ActionType::ShopReroll,
                0);
            addButton(buttonTextX, buttonTextY, prefixedLabel(rerollSlot, "Reroll 2g"), 1.0f,
                      0.20f, 0.16f, 0.08f, &buttonW, &buttonH);
            backendRerollX = buttonTextX - std::max(8.0f, kBackendTextScaleBase * 4.0f);
            backendRerollY = buttonTextY - std::max(5.0f, kBackendTextScaleBase * 2.5f);
            backendRerollW = buttonW;
            backendRerollH = buttonH;
        }
        if (hasShopReadyButton) {
            const int readySlot = game::state::backend_shop::keyboardSlotFor(
                backendShopSnapshot,
                game::state::backend_shop::ActionType::ShopReady,
                0);
            const std::string readyLabel = prefixedLabel(readySlot, "Ready");
            const float textScale = 1.0f * kBackendTextScaleBase;
            const int baseW = stb_easy_font_width(const_cast<char*>(readyLabel.c_str()));
            const float textW = std::max(1.0f, static_cast<float>(baseW) * textScale);
            const float padX = std::max(8.0f, textScale * 4.0f);
            const float padY = std::max(5.0f, textScale * 2.5f);
            const float textX = static_cast<float>(uiW) - textW - padX * 2.0f - 28.0f + padX;
            const float textY = 96.0f;
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

    game::runtime::backend_text::appendTextQuads(
        quads,
        26.0f,
        static_cast<float>(uiH) - 36.0f,
        "Use mouse or keys 1-9",
        1.0f,
        0.72f,
        0.82f,
        0.93f,
        1.0f);

    if (!quads.empty()) {
        services.renderer->drawDebugQuads(quads.data(), quads.size(), uiW, uiH);
    }
}

bool ScriptedState::tryHandleBackendCardKey(InputEvent::Key keyId) {
    int target = -1;
    switch (keyId) {
        case InputEvent::Key::Num1: target = 1; break;
        case InputEvent::Key::Num2: target = 2; break;
        case InputEvent::Key::Num3: target = 3; break;
        case InputEvent::Key::Num4: target = 4; break;
        case InputEvent::Key::Num5: target = 5; break;
        case InputEvent::Key::Num6: target = 6; break;
        case InputEvent::Key::Num7: target = 7; break;
        case InputEvent::Key::Num8: target = 8; break;
        case InputEvent::Key::Num9: target = 9; break;
        default:
            return false;
    }

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

    std::vector<IRenderBackend::DebugQuad> quads;
    quads.reserve(4096);

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
        quads.push_back(bg);

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

        game::runtime::backend_text::appendTextQuads(
            quads, entry.x, entry.y, display, textScale, tr, tg, tb, 1.0f);

        minX = std::min(minX, bg.x);
        minY = std::min(minY, bg.y);
        maxX = std::max(maxX, bg.x + bg.w);
        maxY = std::max(maxY, bg.y + bg.h);
        hasAnyEntry = true;
    }

    if (hasAnyEntry) {
        const auto msgOpt = game::scripting::callStringFunction(script.getScriptTable(), {"get_message"});
        std::string header = msgOpt ? *msgOpt : "Menu";
        game::runtime::backend_text::appendTextQuads(quads,
                                minX,
                                std::max(24.0f, minY - 62.0f),
                                header,
                                3.0f * backendTextMenuScale,
                                0.97f,
                                0.97f,
                                0.98f,
                                1.0f);
        game::runtime::backend_text::appendTextQuads(quads,
                                minX,
                                std::max(52.0f, minY - 30.0f),
                                "Click entries or press 1-9",
                                1.5f * backendTextMenuScale,
                                0.72f,
                                0.84f,
                                0.96f,
                                1.0f);

        IRenderBackend::DebugQuad panel;
        panel.x = std::max(8.0f, minX - 18.0f);
        panel.y = std::max(8.0f, minY - 20.0f);
        panel.w = std::min(static_cast<float>(uiW) - panel.x - 8.0f, (maxX - minX) + 36.0f);
        panel.h = std::min(static_cast<float>(uiH) - panel.y - 8.0f, (maxY - minY) + 28.0f);
        panel.r = 0.05f;
        panel.g = 0.06f;
        panel.b = 0.08f;
        panel.a = 0.70f;
        quads.insert(quads.begin(), panel);
    }

    if (!quads.empty()) {
        services.renderer->drawDebugQuads(quads.data(), quads.size(), uiW, uiH);
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
    int targetOption = -1;
    switch (keyId) {
        case InputEvent::Key::Num1: targetOption = 1; break;
        case InputEvent::Key::Num2: targetOption = 2; break;
        case InputEvent::Key::Num3: targetOption = 3; break;
        case InputEvent::Key::Num4: targetOption = 4; break;
        case InputEvent::Key::Num5: targetOption = 5; break;
        case InputEvent::Key::Num6: targetOption = 6; break;
        case InputEvent::Key::Num7: targetOption = 7; break;
        case InputEvent::Key::Num8: targetOption = 8; break;
        case InputEvent::Key::Num9: targetOption = 9; break;
        default: return false;
    }

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
    if (!services.renderEnabled) {
        sol::table S = script.getScriptTable();
        const bool hasTextMenuEntries = game::scripting::hasFunction(S, "get_text_menu_entries");
        const bool hasTextMenuClick = game::scripting::hasAnyFunction(S, {"on_text_menu_click", "on_menu_click"});
        const bool hasShopCards = game::scripting::hasFunction(S, "get_shop_cards");
        const bool hasShopClick = game::scripting::hasAnyFunction(S, {"on_shop_card_click", "on_card_click", "onCardClick"});
        const bool hasStarterCards = game::scripting::hasFunction(S, "get_starter_cards");
        const bool hasStarterClick = game::scripting::hasAnyFunction(S, {"on_card_click", "onCardClick"});
        hasShopItems = game::scripting::hasFunction(S, "get_shop_items") &&
                       game::scripting::hasFunction(S, "on_shop_item_click");
        hasShopReadyButton = game::scripting::hasFunction(S, "on_shop_ready_click");
        hasShopRerollButton = game::scripting::hasFunction(S, "on_shop_reroll_click");
        hasTextMenu = hasTextMenuEntries && hasTextMenuClick;

        renderWorld = true;
        if (auto hideWorld = S.get<sol::optional<bool>>("hide_world")) {
            renderWorld = !(*hideWorld);
        }

        backendMainButtons.clear();
        backendItemButtons.clear();
        backendShopSnapshot.clear();
        backendRerollX = 0.0f;
        backendRerollY = 0.0f;
        backendRerollW = 0.0f;
        backendRerollH = 0.0f;

        if (hasTextMenu) {
            cardMode = CardMode::TextMenu;
            rebuildTextMenu();
            logHeadlessTextMenuHints();
        } else if (hasShopCards && hasShopClick) {
            cardMode = CardMode::Shop;
            rebuildCardRow();
        } else if (hasStarterCards && hasStarterClick) {
            cardMode = CardMode::Starter;
            rebuildCardRow();
        } else {
            cardMode = CardMode::None;
            hasShopItems = false;
            hasShopReadyButton = false;
            hasShopRerollButton = false;
            if (gameWorld) {
                gameWorld->clearClassicShopCards();
                gameWorld->setUnitDropZoneLayoutHint(0, false);
            }
        }
        uiInitialized = true;
        script.flushCommands();
        return;
    }

    // Script functions/vars live in the script environment now.
    sol::table S = script.getScriptTable();

    bool hasTextMenuEntries = game::scripting::hasFunction(S, "get_text_menu_entries");
    bool hasTextMenuClick = game::scripting::hasAnyFunction(S, {"on_text_menu_click", "on_menu_click"});
    bool hasShopCards = game::scripting::hasFunction(S, "get_shop_cards");
    bool hasShopClick = game::scripting::hasAnyFunction(S, {"on_shop_card_click", "on_card_click", "onCardClick"});
    bool hasStarterCards = game::scripting::hasFunction(S, "get_starter_cards");
    bool hasStarterClick = game::scripting::hasAnyFunction(S, {"on_card_click", "onCardClick"});
    hasShopItems = game::scripting::hasFunction(S, "get_shop_items") &&
                   game::scripting::hasFunction(S, "on_shop_item_click");
    hasShopReadyButton = game::scripting::hasFunction(S, "on_shop_ready_click");
    hasShopRerollButton = game::scripting::hasFunction(S, "on_shop_reroll_click");
    hasTextMenu = hasTextMenuEntries && hasTextMenuClick;
    renderWorld = true;
    if (auto hideWorld = S.get<sol::optional<bool>>("hide_world")) {
        renderWorld = !(*hideWorld);
    }

    if (hasTextMenu) {
        cardMode = CardMode::TextMenu;
    } else if (hasShopCards && hasShopClick) {
        cardMode = CardMode::Shop;
    } else if (hasStarterCards && hasStarterClick) {
        cardMode = CardMode::Starter;
    } else {
        cardMode = CardMode::None;
        hasShopReadyButton = false;
        hasShopRerollButton = false;
        if (gameWorld) {
            gameWorld->clearClassicShopCards();
            gameWorld->setUnitDropZoneLayoutHint(0, false);
        }
        if (shopUi) shopUi->clear();
        uiInitialized = true;
        return;
    }

    const bool useBackendCardUi = shouldUseBackendCardUi() && (cardMode != CardMode::TextMenu);
    if (useBackendCardUi) {
        cardSystem.clearCards();
        itemCardSystem.clearCards();
        if (shopUi) shopUi->clear();
        titleText.reset();
        rebuildCardRow();
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
        backendMainButtons.clear();
        backendItemButtons.clear();
        backendShopSnapshot.clear();
        backendRerollX = 0.0f;
        backendRerollY = 0.0f;
        backendRerollW = 0.0f;
        backendRerollH = 0.0f;
        shopReadyX = 0.0f;
        shopReadyY = 0.0f;
        shopReadyW = 0.0f;
        shopReadyH = 0.0f;
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

    if (titleText) {
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

    if (cardMode == CardMode::TextMenu && services.renderer) {
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
