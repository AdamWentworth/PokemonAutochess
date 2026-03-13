#include "ScriptedState.h"

#include "game/GameStateManager.h"
#include "game/scripting/LuaCardParser.h"
#include "game/scripting/LuaScriptHelpers.h"
#include "game/scripting/LuaTextMenuParser.h"
#include "game/runtime/ui/CardRenderer.h"
#include "game/runtime/ui/DebugText.h"
#include "game/runtime/routes/GameServiceRenderRoutes.h"
#include "game/runtime/ui/SellOverlayModel.h"
#include "game/runtime/ui/ShopHudModel.h"
#include "game/runtime/ui/UiScale.h"
#include "game/state/BackendInputSlots.h"
#include "game/state/PlacementState.h"
#include "game/state/BackendUiPolicy.h"
#include "game/state/ShopCardConversion.h"
#include "game/logging/FlowTrace.h"
#include "game/ui/SellOverlayUiPolicy.h"
#include "game/ui/UIViewport.h"
#include "engine/render/IRenderBackend.h"

#include <sol/sol.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
constexpr float kBackendTextScaleBase = 1.35f;
}

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
            entry.w = std::max(8.0f, game::runtime::ui_text::measureTextWidth(display, textScale));
            entry.h = std::max(8.0f, game::runtime::ui_text::measureTextHeight(display, textScale));

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
    std::vector<CardData> preparedCards = cards;
    game::runtime::ui_card_renderer::prepareCardDataForBackendRender(preparedCards, isItemRow);
    game::runtime::ui_card_renderer::prewarmCardDataTextures(
        services.renderer,
        preparedCards,
        isItemRow);

    game::state::backend_cards::BuildInput in;
    in.cards = std::move(preparedCards);
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
    const bool includeMainRow = !isShopMode || game::ui::sell_overlay::shouldRenderShopCards(showSellOverlay);
    const bool includeItemRow = isShopMode &&
        game::ui::sell_overlay::shouldRenderItemRow(hasShopItems, showSellOverlay);

    game::state::backend_shop::BuildInput input;
    input.shopMode = isShopMode;
    input.mainCount = includeMainRow ? backendMainButtons.size() : 0u;
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
            game::logging::flow::noteStarterCardClick(card.data.pokemonName);
            const double tLuaStart = game::logging::flow::nowMs();
            sol::function onClick = game::scripting::resolveFunction(S, {"on_card_click", "onCardClick"});
            if (onClick.valid()) onClick(card.data.pokemonName);
            const double tLuaEnd = game::logging::flow::nowMs();
            script.flushCommands();
            const double tFlushEnd = game::logging::flow::nowMs();
            if (stateManager) {
                stateManager->pushState(std::make_unique<PlacementState>(
                    stateManager, gameWorld, services, card.data.pokemonName));
            }
            const double tPushEnd = game::logging::flow::nowMs();
            game::logging::flow::log(
                "starter_click_pipeline",
                "pokemon=" + card.data.pokemonName +
                " lua=" + game::logging::flow::formatMs(tLuaEnd - tLuaStart) +
                " flush=" + game::logging::flow::formatMs(tFlushEnd - tLuaEnd) +
                " push_placement=" + game::logging::flow::formatMs(tPushEnd - tFlushEnd));
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
    const float uiScale = game::runtime::ui_scale::viewportScale(uiW, uiH);
    const float edgePad = game::runtime::ui_scale::edgePad(uiW, uiH);
    const float lineStep = game::runtime::ui_scale::lineStep(uiW, uiH);

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
    const bool renderMainRow = !isShopMode || game::ui::sell_overlay::shouldRenderShopCards(showSellOverlay);

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
        const float textW = std::max(1.0f, game::runtime::ui_text::measureTextWidth(label, textScale));
        const float textH = std::max(1.0f, game::runtime::ui_text::measureTextHeight(label, textScale));
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

        game::runtime::ui_text::appendTextLines(
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
        const float textW = std::max(1.0f, game::runtime::ui_text::measureTextWidth(text, textScale));
        const float x = centerX - textW * 0.5f;
        game::runtime::ui_text::appendTextLines(
            textLines, x, y, text, textScale, r, g, b, 1.0f, 0.88f);
    };

    const auto msgOpt = game::scripting::callStringFunction(script.getScriptTable(), {"get_message"});
    const std::string header = msgOpt ? *msgOpt : ((cardMode == CardMode::Starter) ? "Starter" : "Shop");
    game::runtime::ui_text::appendTextLines(
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
            game::runtime::ui_card_renderer::CardRenderInput renderIn;
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
            game::runtime::ui_card_renderer::appendCardLayered(
                baseQuads,
                nullptr,
                &sprites,
                renderIn,
                &textLines);
        }
    };

    if (renderMainRow) {
        addCardRow(
            backendMainButtons,
            isShopMode ? game::state::backend_shop::ActionType::ShopCard
                       : game::state::backend_shop::ActionType::StarterCard,
            /*itemRow=*/false);
    }
    if (game::ui::sell_overlay::shouldRenderItemRow(hasShopItems, showSellOverlay)) {
        addCardRow(backendItemButtons, game::state::backend_shop::ActionType::ItemCard, /*itemRow=*/true);
    }

    const auto sellOverlay = game::runtime::ui_sell_overlay::buildModel(
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
        const std::string moneyLabel = game::runtime::ui_shop_hud::moneyLabel(
            gameWorld ? gameWorld->getMoney() : 0);
        const int rerollSlot = game::state::backend_shop::keyboardSlotFor(
            backendShopSnapshot,
            game::state::backend_shop::ActionType::ShopReroll,
            0);
        const std::string rerollLabel = game::runtime::ui_shop_hud::rerollLabel(rerollSlot);

        int cardsX = 18;
        int cardsY = std::max(0, uiH - 120);
        int cardsH = 96;
        if (!backendMainButtons.empty()) {
            cardsX = game::runtime::ui_shop_hud::cardsAnchorX(backendMainButtons.front().x);
            cardsY = game::runtime::ui_shop_hud::cardsAnchorY(backendMainButtons.front().y, uiH);
            cardsH = game::runtime::ui_shop_hud::cardsAnchorH(backendMainButtons.front().h);
        }

        const float moneyScale = 1.0f * kBackendTextScaleBase * uiScale;
        const float rerollScale = 1.0f * kBackendTextScaleBase * uiScale;
        const float moneyW = game::runtime::ui_text::measureTextWidth(moneyLabel, moneyScale);
        const float moneyH = game::runtime::ui_text::measureTextHeight(moneyLabel, moneyScale);
        const float rerollW = game::runtime::ui_text::measureTextWidth(rerollLabel, rerollScale);
        const float rerollH = game::runtime::ui_text::measureTextHeight(rerollLabel, rerollScale);

        game::runtime::ui_shop_hud::LayoutInput hudIn;
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
        const game::ui::ClassicHudLayout hud = game::runtime::ui_shop_hud::computeLayout(hudIn);

        game::runtime::ui_text::appendTextLines(
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
            const std::string readyLabel = game::runtime::ui_shop_hud::keyboardPrefixedLabel(readySlot, "Ready");
            const float textScale = 1.0f * kBackendTextScaleBase * uiScale;
            const float textW = std::max(1.0f, game::runtime::ui_text::measureTextWidth(readyLabel, textScale));
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

    game::runtime::ui_text::appendTextLines(
        textLines,
        edgePad,
        std::max(4.0f, static_cast<float>(uiH) - edgePad - lineStep * 0.8f),
        game::runtime::ui_shop_hud::interactionHint(),
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
            game::runtime::ui_text::appendTextLines(
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
        game::runtime::ui_text::appendTextLines(
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
        game::runtime::ui_text::appendTextLines(textLines,
                                minX,
                                std::max(24.0f, minY - 62.0f),
                                header,
                                3.0f * backendTextMenuScale,
                                0.97f,
                                0.97f,
                                0.98f,
                                1.0f,
                                0.96f);
        game::runtime::ui_text::appendTextLines(textLines,
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
        const std::string selectedEntryId = entry.id;
        ++option;
        if (option != targetOption) continue;

        sol::table S = script.getScriptTable();
        sol::function onMenuClick = game::scripting::resolveFunction(S, {"on_text_menu_click", "on_menu_click"});
        if (!onMenuClick.valid()) {
            std::cout << "[Menu][BackendUI] Menu click handler unavailable.\n";
            return false;
        }
        const bool startActionEntry = game::logging::flow::isStartActionEntry(selectedEntryId);
        if (startActionEntry) {
            game::logging::flow::noteMenuActionClick(selectedEntryId, scriptPath + ":keyboard");
        }
        std::cout << "[Menu][BackendUI] Selected [" << targetOption << "] " << entry.label << "\n";
        const double tLuaStart = game::logging::flow::nowMs();
        onMenuClick(selectedEntryId);
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
        logHeadlessTextMenuHints();
        return true;
    }

    std::cout << "[Menu][BackendUI] No menu option mapped to key " << targetOption << ".\n";
    return false;
}




