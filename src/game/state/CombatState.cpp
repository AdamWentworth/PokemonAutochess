#include "CombatState.h"

#include "game/GameWorld.h"
#include "game/GameServices.h"
#include "game/logging/LoggerUtil.h"
#include "game/scripting/LuaCardParser.h"
#include "game/scripting/LuaScriptHelpers.h"
#include "game/ui/UIViewport.h"

#include "game/ecs/CombatActive.h"
#include "engine/core/ecs/World.h"
#include "engine/input/InputEvent.h"

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
}

void CombatState::ensureShopUi() {
    if (!services.renderEnabled) {
        shopUiEnabled = false;
        hasShopRerollButton = false;
        if (shopUi) shopUi->clear();
        return;
    }

    sol::table S = script.getScriptTable();
    const bool hasShopCards = game::scripting::hasFunction(S, "get_shop_cards");
    const bool hasShopClick = game::scripting::hasAnyFunction(S, {"on_shop_card_click", "on_card_click", "onCardClick"});
    shopUiEnabled = hasShopCards && hasShopClick;
    hasShopRerollButton = shopUiEnabled && game::scripting::hasFunction(S, "on_shop_reroll_click");

    if (!shopUiEnabled) {
        if (shopUi) shopUi->clear();
        if (gameWorld) {
            gameWorld->clearClassicShopCards();
            gameWorld->setUnitDropZoneLayoutHint(0, false);
        }
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
    if (gameWorld) {
        gameWorld->clearClassicShopCards();
        gameWorld->setUnitDropZoneLayoutHint(0, false);
    }
    script.onExit();
}

void CombatState::handleInput(const InputEvent& event) {
    if (event.type == InputEvent::Type::Resize) {
        if (shopUiEnabled) {
            rebuildShopCards();
        }
    }

    script.call("handleInput");
    if (event.type == InputEvent::Type::MouseDown && gameWorld) {
        if (gameWorld->consumeUiClickBlocked()) return;
        if (gameWorld->isUnitDragActive()) return;
    }
    if (!shopUiEnabled || !shopUi) return;
    if (event.type != InputEvent::Type::MouseDown) return;

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
    if (!services.renderEnabled) return;

    if (!textRenderer) {
        const auto& c = services.config;
        textRenderer = std::make_unique<TextRenderer>(c.fontPath, c.fontSize);
    }
    if (!textRenderer) return;

    const float scale = 1.0f;
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
    const float uiWidth = static_cast<float>(viewport ? viewport->width : 1280);
    float textWidth = textRenderer->measureTextWidth(msg, scale);
    float centeredX = viewport ? viewport->centerX(textWidth)
                               : std::round((uiWidth - textWidth) * 0.5f);

    textRenderer->renderText(msg, centeredX, kHeaderY, msgColor, scale);

    if (shopUiEnabled) {
        const int uiW = viewport ? viewport->width : 1280;
        const int uiH = viewport ? viewport->height : 720;
        const bool showSellOverlay = gameWorld &&
                                     gameWorld->isUnitDragActive() &&
                                     (gameWorld->getUnitDropZoneCardCount() > 0);
        drawShopHud(uiW, uiH, showSellOverlay);
    }
}
