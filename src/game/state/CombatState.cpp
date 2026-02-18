#include "CombatState.h"

#include "game/GameWorld.h"
#include "game/GameServices.h"
#include "game/logging/LoggerUtil.h"
#include "game/runtime/BackendCardRenderer.h"
#include "game/runtime/BackendDebugText.h"
#include "game/runtime/BackendSellOverlayModel.h"
#include "game/runtime/BackendShopHudModel.h"
#include "game/runtime/BackendUiScale.h"
#include "game/scripting/LuaCardParser.h"
#include "game/scripting/LuaScriptHelpers.h"
#include "game/state/BackendInputSlots.h"
#include "game/state/BackendUiPolicy.h"
#include "game/state/ShopCardConversion.h"
#include "game/ui/ShopLayout.h"
#include "game/ui/SellOverlayUiPolicy.h"
#include "game/ui/UIViewport.h"

#include "game/ecs/CombatActive.h"
#include "engine/core/ecs/World.h"
#include "engine/input/InputEvent.h"
#include "engine/render/IRenderBackend.h"

#include "engine/ui/TextRenderer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>
#include <sol/sol.hpp>

namespace {
std::string Capitalize(std::string s) {
    if (s.empty()) return s;
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

} // namespace

namespace {
constexpr float kCombatStartCountdownSec = 3.0f;
constexpr float kCombatEndHoldSec = 3.0f;
constexpr float kHeaderY = 58.0f;
constexpr float kBackendTextScaleBase = 1.35f;
} // namespace

bool CombatState::shouldUseBackendShopUi() const {
    return game::state::backend_ui::shouldUseBackendUi(
        services.renderer != nullptr,
        services.activeRendererBackend);
}

void CombatState::clearBackendShopUiCache() {
    backendShopButtons.clear();
    backendShopSnapshot.clear();
    backendRerollX = 0.0f;
    backendRerollY = 0.0f;
    backendRerollW = 0.0f;
    backendRerollH = 0.0f;
}

void CombatState::rebuildBackendShopUi(const std::vector<CardData>& cards, int uiW, int uiH) {
    clearBackendShopUiCache();
    game::state::backend_cards::BuildInput in;
    in.cards = cards;
    in.uiW = uiW;
    in.uiH = uiH;
    in.mode = game::state::backend_cards::LayoutMode::Shop;
    in.forceItemRow = false;
    backendShopButtons = game::state::backend_cards::buildButtons(in);
}

void CombatState::refreshBackendShopSnapshot() {
    game::state::backend_shop::BuildInput input;
    input.shopMode = true;
    input.mainCount = backendShopButtons.size();
    input.includeReroll = hasShopRerollButton;
    backendShopSnapshot = game::state::backend_shop::buildEntries(input);

    std::vector<game::state::backend_shop::Rect> mainRects;
    mainRects.reserve(backendShopButtons.size());
    for (const auto& button : backendShopButtons) {
        mainRects.push_back(game::state::backend_shop::Rect{
            button.x,
            button.y,
            button.w,
            button.h
        });
    }

    game::state::backend_shop::PlacementInput placement;
    placement.mainRects = &mainRects;
    placement.rerollRect = game::state::backend_shop::Rect{
        backendRerollX,
        backendRerollY,
        backendRerollW,
        backendRerollH
    };
    placement.hasRerollRect = input.includeReroll;
    game::state::backend_shop::applyPlacement(backendShopSnapshot, placement);
}

bool CombatState::invokeBackendShopEntry(const game::state::backend_shop::Entry& entry) {
    sol::table S = script.getScriptTable();

    switch (entry.action) {
        case game::state::backend_shop::ActionType::ShopCard: {
            if (entry.sourceIndex >= backendShopButtons.size()) return false;
            const auto& card = backendShopButtons[entry.sourceIndex];
            sol::function onClick = game::scripting::resolveFunction(
                S, {"on_shop_card_click", "on_card_click", "onCardClick"});
            if (onClick.valid()) {
                onClick(card.data.pokemonName, card.data.level);
            }
            script.flushCommands();
            rebuildShopCards();
            return true;
        }
        case game::state::backend_shop::ActionType::ShopReroll: {
            sol::function onReroll = game::scripting::resolveFunction(S, {"on_shop_reroll_click"});
            if (onReroll.valid()) {
                onReroll();
            }
            script.flushCommands();
            rebuildShopCards();
            return true;
        }
        default:
            return false;
    }
}

bool CombatState::tryHandleBackendShopKey(InputEvent::Key keyId) {
    const int target = game::state::backend_input::slotFromNumberKey(keyId);

    if (target <= 0) return false;
    refreshBackendShopSnapshot();
    const auto* entry = game::state::backend_shop::findByKeyboardSlot(backendShopSnapshot, target);
    if (!entry) return false;
    return invokeBackendShopEntry(*entry);
}

bool CombatState::handleBackendShopMouseClick(int mouseX, int mouseY) {
    const float mx = static_cast<float>(mouseX);
    const float my = static_cast<float>(mouseY);
    refreshBackendShopSnapshot();
    const auto* entry = game::state::backend_shop::findByPoint(backendShopSnapshot, mx, my);
    if (!entry) return false;
    return invokeBackendShopEntry(*entry);
}

void CombatState::renderBackendShopUi(int uiW, int uiH, bool showSellOverlay, const std::string& header) {
    if (!services.renderer) return;
    const float uiScale = game::runtime::backend_ui::viewportScale(uiW, uiH);
    const float edgePad = game::runtime::backend_ui::edgePad(uiW, uiH);
    const float lineStep = game::runtime::backend_ui::lineStep(uiW, uiH);

    std::vector<IRenderBackend::DebugQuad> baseQuads;
    baseQuads.reserve(4096);
    std::vector<IRenderBackend::DebugQuad> textQuads;
    textQuads.reserve(4096);
    std::vector<IRenderBackend::DebugSprite> sprites;
    sprites.reserve(1024);

    const auto appendText = [&](float x,
                                float y,
                                const std::string& text,
                                float scale,
                                float r,
                                float g,
                                float b) {
        game::runtime::backend_text::appendTextQuads(
            textQuads,
            x,
            y,
            text,
            std::max(0.1f, scale) * kBackendTextScaleBase * uiScale,
            r,
            g,
            b,
            1.0f);
    };

    backendRerollX = 0.0f;
    backendRerollY = 0.0f;
    backendRerollW = 0.0f;
    backendRerollH = 0.0f;
    refreshBackendShopSnapshot();

    appendText(edgePad, std::max(10.0f, edgePad - lineStep * 0.15f), header, 1.9f, 0.97f, 0.97f, 0.99f);

    if (game::ui::sell_overlay::shouldRenderShopCards(showSellOverlay)) {
        for (std::size_t i = 0; i < backendShopButtons.size(); ++i) {
            const auto& button = backendShopButtons[i];

            const int slot = game::state::backend_shop::keyboardSlotFor(
                backendShopSnapshot,
                game::state::backend_shop::ActionType::ShopCard,
                i);
            const std::string label = button.data.label.empty()
                ? button.data.pokemonName
                : button.data.label;

            std::string sub = "Lv " + std::to_string(std::max(1, button.data.level));
            sub += "  Cost " + std::to_string(std::max(0, button.data.cost)) + "g";
            game::runtime::backend_card_renderer::CardRenderInput renderIn;
            renderIn.x = button.x;
            renderIn.y = button.y;
            renderIn.w = button.w;
            renderIn.h = button.h;
            renderIn.displayName = label;
            renderIn.speciesName = button.data.pokemonName;
            renderIn.subtitle = sub;
            renderIn.explicitImagePath = button.data.imagePath;
            renderIn.u0 = button.data.uvMin.x;
            renderIn.v0 = button.data.uvMin.y;
            renderIn.u1 = button.data.uvMax.x;
            renderIn.v1 = button.data.uvMax.y;
            renderIn.keyboardSlot = slot;
            renderIn.item = (button.data.type == CardType::Item);
            renderIn.textScale = std::clamp(0.74f * uiScale, 0.55f, 1.10f);
            renderIn.spriteAlpha = 1.0f;
            game::runtime::backend_card_renderer::appendCardLayered(baseQuads, textQuads, &sprites, renderIn);
        }
    }

    const std::string moneyLabel = game::runtime::backend_shop_hud::moneyLabel(
        gameWorld ? gameWorld->getMoney() : 0);
    const std::string rerollLabel = game::runtime::backend_shop_hud::rerollLabel(
        game::state::backend_shop::keyboardSlotFor(
            backendShopSnapshot,
            game::state::backend_shop::ActionType::ShopReroll,
            0));

    const float moneyScale = 1.0f * kBackendTextScaleBase * uiScale;
    const float rerollScale = 0.78f * kBackendTextScaleBase * uiScale;
    const float moneyW = game::runtime::backend_text::measureTextWidth(moneyLabel, moneyScale);
    const float moneyH = game::runtime::backend_text::measureTextHeight(moneyLabel, moneyScale);
    const float rerollW = game::runtime::backend_text::measureTextWidth(rerollLabel, rerollScale);
    const float rerollH = game::runtime::backend_text::measureTextHeight(rerollLabel, rerollScale);

    int cardsX = 18;
    int cardsY = std::max(0, uiH - 120);
    int cardsH = 96;
    if (!backendShopButtons.empty()) {
        cardsX = game::runtime::backend_shop_hud::cardsAnchorX(backendShopButtons.front().x);
        cardsY = game::runtime::backend_shop_hud::cardsAnchorY(backendShopButtons.front().y, uiH);
        cardsH = game::runtime::backend_shop_hud::cardsAnchorH(backendShopButtons.front().h);
    }

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

    {
        IRenderBackend::DebugQuad moneyPanel;
        moneyPanel.x = std::max(8.0f, hud.textX - 8.0f);
        moneyPanel.y = std::max(8.0f, hud.textY - 6.0f);
        moneyPanel.w = moneyW + 16.0f;
        moneyPanel.h = moneyH + 12.0f;
        moneyPanel.r = 0.12f;
        moneyPanel.g = 0.18f;
        moneyPanel.b = 0.10f;
        moneyPanel.a = 0.86f;
        baseQuads.push_back(moneyPanel);
        appendText(hud.textX, hud.textY, moneyLabel, 0.74f, 0.95f, 0.88f, 0.50f);
    }

    if (hasShopRerollButton) {
        IRenderBackend::DebugQuad rerollPanel;
        rerollPanel.x = std::max(8.0f, hud.rerollX - 8.0f);
        rerollPanel.y = std::max(8.0f, hud.rerollY - 6.0f);
        rerollPanel.w = rerollW + 16.0f;
        rerollPanel.h = rerollH + 12.0f;
        rerollPanel.r = 0.20f;
        rerollPanel.g = 0.16f;
        rerollPanel.b = 0.08f;
        rerollPanel.a = 0.90f;
        baseQuads.push_back(rerollPanel);

        appendText(hud.rerollX, hud.rerollY, rerollLabel, 0.58f, 0.98f, 0.96f, 0.90f);

        backendRerollX = rerollPanel.x;
        backendRerollY = rerollPanel.y;
        backendRerollW = rerollPanel.w;
        backendRerollH = rerollPanel.h;
    }

    const auto sellOverlay = game::runtime::backend_sell_overlay::buildModel(
        showSellOverlay && gameWorld != nullptr,
        uiW,
        uiH,
        gameWorld ? gameWorld->getUnitDropZoneCardCount() : 0,
        gameWorld ? gameWorld->getUnitDropZoneUsesItemLayout() : false,
        gameWorld ? gameWorld->isUnitSellRewardsEnabled() : true);
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

            const float titleW = game::runtime::backend_text::measureTextWidth(
                sellOverlay.copy.title, sellOverlay.copy.titleScale * kBackendTextScaleBase);
            const float hintW = game::runtime::backend_text::measureTextWidth(
                sellOverlay.copy.hint, sellOverlay.copy.hintScale * kBackendTextScaleBase);
            appendText(
                sellOverlay.centerX - titleW * 0.5f,
                sellOverlay.titleY,
                sellOverlay.copy.title,
                sellOverlay.copy.titleScale,
                0.99f,
                0.95f,
                0.90f);
            appendText(
                sellOverlay.centerX - hintW * 0.5f,
                sellOverlay.hintY,
                sellOverlay.copy.hint,
                sellOverlay.copy.hintScale,
                0.98f,
                0.86f,
                0.82f);
    }

    refreshBackendShopSnapshot();
    appendText(edgePad,
               std::max(4.0f, static_cast<float>(uiH) - edgePad - lineStep * 0.8f),
               game::runtime::backend_shop_hud::interactionHint(),
               0.74f,
               0.72f,
               0.82f,
               0.93f);

    if (!baseQuads.empty()) {
        services.renderer->drawDebugQuads(baseQuads.data(), baseQuads.size(), uiW, uiH);
    }
    if (!sprites.empty()) {
        services.renderer->drawDebugSprites(sprites.data(), sprites.size(), uiW, uiH);
    }
    if (!textQuads.empty()) {
        services.renderer->drawDebugQuads(textQuads.data(), textQuads.size(), uiW, uiH);
    }
}

void CombatState::ensureShopUi() {
    sol::table S = script.getScriptTable();
    const bool hasShopCards = game::scripting::hasFunction(S, "get_shop_cards");
    const bool hasShopClick = game::scripting::hasAnyFunction(S, {"on_shop_card_click", "on_card_click", "onCardClick"});
    shopUiEnabled = hasShopCards && hasShopClick;
    hasShopRerollButton = shopUiEnabled && game::scripting::hasFunction(S, "on_shop_reroll_click");

    if (!shopUiEnabled) {
        if (shopUi) shopUi->clear();
        clearBackendShopUiCache();
        if (gameWorld) {
            gameWorld->clearClassicShopCards();
            gameWorld->setUnitDropZoneLayoutHint(0, false);
        }
        return;
    }

    if (shouldUseBackendShopUi()) {
        if (shopUi) shopUi->clear();
        shopUi.reset();
        shopUiInitialized = false;
        rebuildShopCards();
        return;
    }

    clearBackendShopUiCache();

    if (!services.renderEnabled) {
        if (shopUi) shopUi->clear();
        shopUiEnabled = false;
        hasShopRerollButton = false;
        return;
    }

    if (!shopUiInitialized) {
        shopUi = std::make_unique<game::ui::ShopUiFacade>();
        shopUi->init(services.config.fontPath, std::max(28, services.config.fontSize / 2),
                     std::max(16, services.config.fontSize / 2));
        shopUiInitialized = true;
    }
    rebuildShopCards();
}

void CombatState::rebuildShopCards() {
    if (!shopUiEnabled) return;

    sol::table S = script.getScriptTable();
    sol::protected_function getCards = S["get_shop_cards"];
    std::vector<CardData> cards;
    std::string parseError;
    if (!game::scripting::parseCardList(getCards, cards, &parseError)) {
        if (!parseError.empty()) {
            game::log::warn(&services.log, std::string("[CombatState] failed to parse shop cards: ") + parseError);
        }
        if (shopUi) shopUi->clear();
        if (gameWorld) {
            gameWorld->clearClassicShopCards();
            gameWorld->setUnitDropZoneLayoutHint(0, false);
        }
        return;
    }

    bool allItems = true;
    for (const auto& card : cards) {
        if (card.type != CardType::Item) {
            allItems = false;
            break;
        }
    }

    if (gameWorld) {
        gameWorld->setClassicShopCards(game::state::shop_cards::toClassicCards(cards));
        gameWorld->setUnitDropZoneLayoutHint(static_cast<int>(cards.size()), allItems);
        gameWorld->setUnitSellRewardsEnabled(services.gameMode == "classic");
    }

    const auto* viewport = services.viewport;
    const int uiW = viewport ? viewport->width : 1280;
    const int uiH = viewport ? viewport->height : 720;
    if (shouldUseBackendShopUi()) {
        rebuildBackendShopUi(cards, uiW, uiH);
        refreshBackendShopSnapshot();
        if (shopUi) shopUi->clear();
        return;
    }

    clearBackendShopUiCache();
    if (shopUi) shopUi->setCards(cards, uiW, uiH);
}

void CombatState::drawShopHud(int uiW, int uiH, bool showSellOverlay) {
    if (!shopUiEnabled || !shopUi || !shopUi->hasCards()) {
        return;
    }

    game::ui::ShopUiRenderInput in;
    in.uiW = uiW;
    in.uiH = uiH;
    in.money = gameWorld ? gameWorld->getMoney() : 0;
    in.showReroll = hasShopRerollButton;
    in.gameMode = services.gameMode;
    in.moneyScale = 1.35f;
    in.rerollScale = 0.92f;
    in.rerollLabel = "[Reroll 2g]";
    in.showSellOverlay = showSellOverlay;
    in.sellOverlayPaysMoney = gameWorld ? gameWorld->isUnitSellRewardsEnabled() : true;
    shopUi->render(in);
}

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

void CombatState::setCombatActiveFlag(bool active) {
    if (!services.ecsWorld || !services.ecsWorld->alive(services.combatStateEntity)) return;
    if (auto* state = services.ecsWorld->get<game::CombatActive>(services.combatStateEntity)) {
        state->active = active;
    } else {
        services.ecsWorld->add<game::CombatActive>(services.combatStateEntity, game::CombatActive{active});
    }
}

bool CombatState::shouldDelayPostCombat() const {
    if (!gameWorld) return false;
    bool anyEnemyAlive = false;
    bool anyPlayerAlive = false;
    for (const auto& u : gameWorld->getPokemons()) {
        if (u.side == PokemonSide::Enemy) {
            if (u.alive || u.captureInProgress) anyEnemyAlive = true;
        } else if (u.side == PokemonSide::Player) {
            if (u.alive) anyPlayerAlive = true;
        }
    }
    return anyPlayerAlive && !anyEnemyAlive;
}

void CombatState::onEnter() {
    combatStarted = false;
    postCombatHoldActive = false;
    preCombatCountdownSec = kCombatStartCountdownSec;
    postCombatCountdownSec = 0.0f;

    if (gameWorld) {
        gameWorld->resetCombatBalance();
        gameWorld->capturePlayerPositionsForBattle();
        gameWorld->setBoardInteractionLocked(true);

        // Cleanup: remove any dead units and all previous enemies before spawning new wave.
        auto& list = gameWorld->getPokemons();
        list.erase(
            std::remove_if(list.begin(), list.end(),
                [](const PokemonInstance& u) {
                    return (!u.alive) || (u.side == PokemonSide::Enemy);
                }),
            list.end()
        );
    }

    setCombatActiveFlag(false);

    sol::table S = script.getScriptTable();

    if (const auto msg = game::scripting::callStringFunction(S, {"get_message"})) {
        combatMessage = *msg;
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

    if (sol::function get_balance = S["get_combat_balance"]; get_balance.valid()) {
        sol::protected_function_result r = get_balance();
        if (r.valid() && r.get_type() == sol::type::table) {
            sol::table t = r;
            GameWorld::CombatBalance b;

            if (auto v = t.get<sol::optional<float>>("playerDamageMult"); v) b.playerDamageMult = *v;
            if (auto v = t.get<sol::optional<float>>("enemyDamageMult"); v) b.enemyDamageMult = *v;
            if (auto v = t.get<sol::optional<float>>("playerDamageTakenMult"); v) b.playerDamageTakenMult = *v;
            if (auto v = t.get<sol::optional<float>>("enemyDamageTakenMult"); v) b.enemyDamageTakenMult = *v;

            if (gameWorld) gameWorld->setCombatBalance(b);
        }
    }

    // Player send-out lines (optional flavor)
    for (auto& u : gameWorld->getPokemons()) {
        if (!u.alive) continue;
        if (u.side == PokemonSide::Player) {
            game::log::info(&services.log, "Go! " + Capitalize(u.name) + "!");
        }
    }

    script.onEnter();
    ensureShopUi();
}

void CombatState::onExit() {
    setCombatActiveFlag(false);
    if (gameWorld) {
        gameWorld->restorePlayerPositionsAfterBattle();
        gameWorld->setBoardInteractionLocked(false);
        gameWorld->healPlayerUnitsToFull();
        gameWorld->resetCombatBalance();
    }
    if (shopUi) shopUi->clear();
    shopUiEnabled = false;
    hasShopRerollButton = false;
    shopUiInitialized = false;
    shopUi.reset();
    clearBackendShopUiCache();
    if (gameWorld) {
        gameWorld->clearClassicShopCards();
        gameWorld->setUnitDropZoneLayoutHint(0, false);
    }
    script.onExit();
}

void CombatState::handleInput(const InputEvent& event) {
    if (event.type == InputEvent::Type::Resize) {
        if (shopUiEnabled || !backendShopButtons.empty()) {
            rebuildShopCards();
        }
    }

    script.call("handleInput");
    if (event.type == InputEvent::Type::MouseDown && gameWorld) {
        if (gameWorld->consumeUiClickBlocked()) return;
        if (gameWorld->isUnitDragActive()) return;
    }

    if (shopUiEnabled &&
        shouldUseBackendShopUi() &&
        event.type == InputEvent::Type::KeyDown &&
        !event.repeat) {
        if (tryHandleBackendShopKey(event.keyId)) {
            return;
        }
    }

    if (event.type != InputEvent::Type::MouseDown) return;

    if (shopUiEnabled && shouldUseBackendShopUi()) {
        if (handleBackendShopMouseClick(event.mouseX, event.mouseY)) {
            return;
        }
    }

    if (!shopUiEnabled || !shopUi) return;

    sol::table S = script.getScriptTable();
    const game::ui::ShopUiClickResult click = shopUi->handleMouseDown(event.mouseX, event.mouseY);
    if (click.rerollClicked && hasShopRerollButton) {
        sol::function onReroll = game::scripting::resolveFunction(S, {"on_shop_reroll_click"});
        if (onReroll.valid()) onReroll();
        script.flushCommands();
        rebuildShopCards();
        return;
    }
    if (!click.cardClicked) return;

    sol::function onClick = game::scripting::resolveFunction(S, {"on_shop_card_click", "on_card_click", "onCardClick"});
    if (onClick.valid()) {
        onClick(click.cardClicked->pokemonName, click.cardClicked->level);
        script.flushCommands();
        rebuildShopCards();
    }
}

void CombatState::update(float dt) {
    if (!combatStarted) {
        preCombatCountdownSec = std::max(0.0f, preCombatCountdownSec - std::max(0.0f, dt));
        if (preCombatCountdownSec <= 0.0f) {
            combatStarted = true;
            setCombatActiveFlag(true);
        }
        return;
    }

    if (shouldDelayPostCombat()) {
        if (!postCombatHoldActive) {
            postCombatHoldActive = true;
            postCombatCountdownSec = kCombatEndHoldSec;
            setCombatActiveFlag(false);
        }
        postCombatCountdownSec = std::max(0.0f, postCombatCountdownSec - std::max(0.0f, dt));
        if (postCombatCountdownSec > 0.0f) {
            return;
        }
    } else {
        if (postCombatHoldActive) {
            postCombatHoldActive = false;
            postCombatCountdownSec = 0.0f;
            setCombatActiveFlag(true);
        }
    }

    script.onUpdate(dt);
}

void CombatState::render() {
    std::string msg = combatMessage.empty() ? std::string("Combat") : combatMessage;
    glm::vec3 msgColor(1.0f, 1.0f, 1.0f);
    if (!combatStarted) {
        const int sec = std::max(1, static_cast<int>(std::ceil(preCombatCountdownSec)));
        msg = "Battle starts in: " + std::to_string(sec);
        msgColor = glm::vec3(1.0f, 1.0f, 0.0f);
    } else if (postCombatHoldActive && postCombatCountdownSec > 0.0f) {
        const int sec = std::max(1, static_cast<int>(std::ceil(postCombatCountdownSec)));
        msg = "Round ending in: " + std::to_string(sec);
        msgColor = glm::vec3(1.0f, 1.0f, 0.0f);
    }

    const auto* viewport = services.viewport;
    const int uiW = viewport ? viewport->width : 1280;
    const int uiH = viewport ? viewport->height : 720;
    const float uiWidth = static_cast<float>(uiW);
    const bool showSellOverlay = gameWorld &&
                                 gameWorld->isUnitDragActive() &&
                                 (gameWorld->getUnitDropZoneCardCount() > 0);

    if (shopUiEnabled && shouldUseBackendShopUi()) {
        renderBackendShopUi(uiW, uiH, showSellOverlay, msg);
        return;
    }

    if (!services.renderEnabled) {
        if (!services.renderer) return;

        std::vector<IRenderBackend::DebugQuad> quads;
        quads.reserve(1024);

        const float scale = 2.0f;
        const float textW = game::runtime::backend_text::measureTextWidth(msg, scale);
        const float textH = game::runtime::backend_text::measureTextHeight(msg, scale);
        const float textX = std::max(14.0f, (uiWidth - textW) * 0.5f);
        const float textY = kHeaderY;

        IRenderBackend::DebugQuad panel;
        panel.x = std::max(8.0f, textX - 14.0f);
        panel.y = std::max(8.0f, textY - 10.0f);
        panel.w = std::min(static_cast<float>(uiW) - panel.x - 8.0f, textW + 28.0f);
        panel.h = std::max(20.0f, textH + 16.0f);
        panel.r = 0.08f;
        panel.g = 0.10f;
        panel.b = 0.12f;
        panel.a = 0.82f;
        quads.push_back(panel);

        game::runtime::backend_text::appendTextQuads(
            quads, textX, textY, msg, scale, msgColor.r, msgColor.g, msgColor.b, 1.0f);

        services.renderer->drawDebugQuads(quads.data(), quads.size(), uiW, uiH);
        return;
    }

    if (!textRenderer) {
        const auto& c = services.config;
        textRenderer = std::make_unique<TextRenderer>(c.fontPath, c.fontSize);
    }
    if (!textRenderer) return;

    const float scale = 1.0f;
    float textWidth = textRenderer->measureTextWidth(msg, scale);
    float centeredX = viewport ? viewport->centerX(textWidth)
                               : std::round((uiWidth - textWidth) * 0.5f);

    textRenderer->renderText(msg, centeredX, kHeaderY, msgColor, scale);

    if (shopUiEnabled) {
        drawShopHud(uiW, uiH, showSellOverlay);
    }
}
