#include "CombatState.h"

#include "game/GameWorld.h"
#include "game/GameServices.h"
#include "game/logging/LoggerUtil.h"
#include "game/runtime/BackendDebugText.h"
#include "game/scripting/LuaCardParser.h"
#include "game/scripting/LuaScriptHelpers.h"
#include "game/state/BackendUiPolicy.h"
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

bool CombatState::shouldUseBackendShopUi() const {
    return game::state::backend_ui::shouldUseBackendUi(
        services.renderer != nullptr,
        services.activeRendererBackend);
}

void CombatState::rebuildBackendShopUi(const std::vector<CardData>& cards, int uiW, int uiH) {
    backendShopButtons.clear();
    backendShopSnapshot.clear();
    backendRerollX = 0.0f;
    backendRerollY = 0.0f;
    backendRerollW = 0.0f;
    backendRerollH = 0.0f;

    if (cards.empty()) return;

    bool allItems = true;
    for (const auto& card : cards) {
        if (card.type != CardType::Item) {
            allItems = false;
            break;
        }
    }

    const game::ui::ShopRowLayout layout = game::ui::computeShopRowLayout(uiW, uiH, allItems);
    const game::ui::ShopRowPlacement place =
        game::ui::computeShopRowPlacement(uiW, uiH, static_cast<int>(cards.size()), layout);
    backendShopButtons.reserve(cards.size());
    for (std::size_t i = 0; i < cards.size(); ++i) {
        BackendCardButton button;
        button.data = cards[i];
        button.x = static_cast<float>(place.startX + static_cast<int>(i) * (layout.cardW + layout.spacing));
        button.y = static_cast<float>(place.y);
        button.w = static_cast<float>(layout.cardW);
        button.h = static_cast<float>(layout.cardH);
        backendShopButtons.push_back(std::move(button));
    }
}

void CombatState::refreshBackendShopSnapshot() {
    game::state::backend_shop::BuildInput input;
    input.shopMode = true;
    input.mainCount = backendShopButtons.size();
    input.includeReroll = hasShopRerollButton;
    backendShopSnapshot = game::state::backend_shop::buildEntries(input);

    for (auto& entry : backendShopSnapshot) {
        switch (entry.action) {
            case game::state::backend_shop::ActionType::ShopCard: {
                if (entry.sourceIndex >= backendShopButtons.size()) break;
                const auto& button = backendShopButtons[entry.sourceIndex];
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
            default:
                break;
        }
    }
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

    std::vector<IRenderBackend::DebugQuad> quads;
    quads.reserve(4096);

    const auto appendText = [&](float x,
                                float y,
                                const std::string& text,
                                float scale,
                                float r,
                                float g,
                                float b) {
        game::runtime::backend_text::appendTextQuads(
            quads,
            x,
            y,
            text,
            std::max(0.1f, scale) * kBackendTextScaleBase,
            r,
            g,
            b,
            1.0f);
    };

    const auto prefixedLabel = [&](int slot, const std::string& label) {
        if (slot > 0 && slot <= 9) {
            return "[" + std::to_string(slot) + "] " + label;
        }
        return label;
    };

    backendRerollX = 0.0f;
    backendRerollY = 0.0f;
    backendRerollW = 0.0f;
    backendRerollH = 0.0f;
    refreshBackendShopSnapshot();

    appendText(26.0f, 24.0f, header, 1.9f, 0.97f, 0.97f, 0.99f);

    if (game::ui::sell_overlay::shouldRenderShopCards(showSellOverlay)) {
        for (std::size_t i = 0; i < backendShopButtons.size(); ++i) {
            const auto& button = backendShopButtons[i];

            IRenderBackend::DebugQuad panel;
            panel.x = button.x;
            panel.y = button.y;
            panel.w = button.w;
            panel.h = button.h;
            panel.r = (button.data.type == CardType::Item) ? 0.26f : 0.14f;
            panel.g = (button.data.type == CardType::Item) ? 0.20f : 0.20f;
            panel.b = (button.data.type == CardType::Item) ? 0.10f : 0.28f;
            panel.a = 0.92f;
            quads.push_back(panel);

            IRenderBackend::DebugQuad border;
            border.x = button.x + 1.0f;
            border.y = button.y + 1.0f;
            border.w = std::max(0.0f, button.w - 2.0f);
            border.h = std::max(0.0f, button.h - 2.0f);
            border.r = 0.06f;
            border.g = 0.07f;
            border.b = 0.10f;
            border.a = 0.40f;
            quads.push_back(border);

            const int slot = game::state::backend_shop::keyboardSlotFor(
                backendShopSnapshot,
                game::state::backend_shop::ActionType::ShopCard,
                i);
            const std::string label = button.data.label.empty() ? button.data.pokemonName : button.data.label;
            appendText(button.x + 8.0f, button.y + 8.0f, prefixedLabel(slot, label), 0.78f, 0.97f, 0.97f, 0.99f);

            std::string sub = "Lv " + std::to_string(std::max(1, button.data.level));
            sub += "  Cost " + std::to_string(std::max(0, button.data.cost)) + "g";
            appendText(button.x + 8.0f, button.y + std::max(16.0f, button.h - 24.0f), sub, 0.70f, 0.83f, 0.90f, 0.96f);
        }
    }

    const std::string moneyLabel =
        "Gold: " + std::to_string(std::max(0, gameWorld ? gameWorld->getMoney() : 0));
    const std::string rerollLabel = prefixedLabel(
        game::state::backend_shop::keyboardSlotFor(
            backendShopSnapshot,
            game::state::backend_shop::ActionType::ShopReroll,
            0),
        "Reroll 2g");

    const float moneyScale = 1.0f * kBackendTextScaleBase;
    const float rerollScale = 0.78f * kBackendTextScaleBase;
    const float moneyW = game::runtime::backend_text::measureTextWidth(moneyLabel, moneyScale);
    const float moneyH = game::runtime::backend_text::measureTextHeight(moneyLabel, moneyScale);
    const float rerollW = game::runtime::backend_text::measureTextWidth(rerollLabel, rerollScale);
    const float rerollH = game::runtime::backend_text::measureTextHeight(rerollLabel, rerollScale);

    int cardsX = 18;
    int cardsY = std::max(0, uiH - 120);
    int cardsH = 96;
    if (!backendShopButtons.empty()) {
        cardsX = static_cast<int>(std::round(backendShopButtons.front().x));
        cardsY = static_cast<int>(std::round(backendShopButtons.front().y));
        cardsH = static_cast<int>(std::round(backendShopButtons.front().h));
    }

    game::ui::ClassicHudLayoutInput hudIn;
    hudIn.uiW = uiW;
    hudIn.uiH = uiH;
    hudIn.shopCardsX = cardsX;
    hudIn.shopCardsY = cardsY;
    hudIn.shopCardsH = cardsH;
    hudIn.moneyTextW = moneyW;
    hudIn.moneyTextH = moneyH;
    hudIn.rerollTextW = rerollW;
    hudIn.rerollTextH = rerollH;
    hudIn.showReroll = hasShopRerollButton;
    hudIn.iconVisible = false;
    const game::ui::ClassicHudLayout hud = game::ui::computeClassicHudLayout(hudIn);

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
        quads.push_back(moneyPanel);
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
        quads.push_back(rerollPanel);

        appendText(hud.rerollX, hud.rerollY, rerollLabel, 0.58f, 0.98f, 0.96f, 0.90f);

        backendRerollX = rerollPanel.x;
        backendRerollY = rerollPanel.y;
        backendRerollW = rerollPanel.w;
        backendRerollH = rerollPanel.h;
    }

    if (showSellOverlay && gameWorld) {
        const game::ui::SellDropZoneLayout outer = game::state::backend_ui::computeSellOverlayOuterLayout(
            uiW,
            uiH,
            gameWorld->getUnitDropZoneCardCount(),
            gameWorld->getUnitDropZoneUsesItemLayout());
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

            const game::ui::sell_overlay::Copy copy =
                game::ui::sell_overlay::makeCopy(gameWorld->isUnitSellRewardsEnabled());
            const float titleW = game::runtime::backend_text::measureTextWidth(
                copy.title, copy.titleScale * kBackendTextScaleBase);
            const float hintW = game::runtime::backend_text::measureTextWidth(
                copy.hint, copy.hintScale * kBackendTextScaleBase);
            const float cx = static_cast<float>(outer.x) + static_cast<float>(outer.w) * 0.5f;
            appendText(
                cx - titleW * 0.5f,
                static_cast<float>(outer.y) + 10.0f,
                copy.title,
                copy.titleScale,
                0.99f,
                0.95f,
                0.90f);
            appendText(
                cx - hintW * 0.5f,
                static_cast<float>(outer.y) + static_cast<float>(outer.h) * 0.58f,
                copy.hint,
                copy.hintScale,
                0.98f,
                0.86f,
                0.82f);
        }
    }

    refreshBackendShopSnapshot();
    appendText(26.0f, static_cast<float>(uiH) - 34.0f, "Use mouse or keys 1-9", 0.74f, 0.72f, 0.82f, 0.93f);

    if (!quads.empty()) {
        services.renderer->drawDebugQuads(quads.data(), quads.size(), uiW, uiH);
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
        backendShopButtons.clear();
        backendShopSnapshot.clear();
        backendRerollX = 0.0f;
        backendRerollY = 0.0f;
        backendRerollW = 0.0f;
        backendRerollH = 0.0f;
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

    backendShopButtons.clear();
    backendShopSnapshot.clear();
    backendRerollX = 0.0f;
    backendRerollY = 0.0f;
    backendRerollW = 0.0f;
    backendRerollH = 0.0f;

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
        gameWorld->setClassicShopCards(buildClassicCardsFromUi(cards));
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

    backendShopButtons.clear();
    backendShopSnapshot.clear();
    backendRerollX = 0.0f;
    backendRerollY = 0.0f;
    backendRerollW = 0.0f;
    backendRerollH = 0.0f;
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
    backendShopButtons.clear();
    backendShopSnapshot.clear();
    backendRerollX = 0.0f;
    backendRerollY = 0.0f;
    backendRerollW = 0.0f;
    backendRerollH = 0.0f;
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
