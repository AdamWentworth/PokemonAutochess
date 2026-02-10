#include "CombatState.h"

#include "game/GameWorld.h"
#include "game/GameServices.h"
#include "game/logging/LoggerUtil.h"
#include "game/ui/UIViewport.h"

#include "game/ecs/CombatActive.h"
#include "engine/core/ecs/World.h"
#include "engine/input/InputEvent.h"

#include "engine/ui/TextRenderer.h"

#include <algorithm>
#include <cmath>
#include <sol/sol.hpp>

namespace {
std::string Capitalize(std::string s) {
    if (s.empty()) return s;
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}
} // namespace

bool CombatState::buildShopCardList(sol::protected_function fn, std::vector<CardData>& out) {
    out.clear();
    if (!fn.valid()) return false;

    sol::protected_function_result r = fn();
    if (!(r.valid() && r.get_type() == sol::type::table)) {
        return false;
    }

    sol::table t = r;
    for (auto&& kv : t) {
        if (kv.second.get_type() != sol::type::table) continue;
        sol::table row = kv.second.as<sol::table>();

        CardData cd;
        cd.pokemonName = row.get_or("name", std::string());
        cd.cost = row.get_or("cost", 0);
        cd.level = row.get_or("level", 0);
        cd.label = row.get_or("label", std::string());
        cd.imagePath = row.get_or("image", std::string());

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

        if (!cd.pokemonName.empty() || !cd.label.empty()) {
            out.push_back(cd);
        }
    }
    return true;
}

void CombatState::ensureShopUi() {
    sol::table S = script.getScriptTable();
    const bool hasShopCards = S["get_shop_cards"].valid();
    const bool hasShopClick = S["on_shop_card_click"].valid() || S["on_card_click"].valid() || S["onCardClick"].valid();
    shopUiEnabled = hasShopCards && hasShopClick;
    hasShopRerollButton = shopUiEnabled && S["on_shop_reroll_click"].valid();

    if (!shopUiEnabled) {
        shopCardSystem.clearCards();
        shopCardsValid = false;
        shopRerollX = 0.0f;
        shopRerollY = 0.0f;
        shopRerollW = 0.0f;
        shopRerollH = 0.0f;
        return;
    }

    if (!shopUiInitialized) {
        shopCardSystem.init();
        shopCardSystem.initOverlayText(services.config.fontPath, std::max(16, services.config.fontSize / 2));
        shopHudText = std::make_unique<TextRenderer>(services.config.fontPath, std::max(28, services.config.fontSize / 2));
        shopUiInitialized = true;
    }

    rebuildShopCards();
}

void CombatState::rebuildShopCards() {
    if (!shopUiEnabled) return;

    sol::table S = script.getScriptTable();
    sol::protected_function getCards = S["get_shop_cards"];
    std::vector<CardData> cards;
    if (!buildShopCardList(getCards, cards)) {
        shopCardSystem.clearCards();
        shopCardsValid = false;
        return;
    }

    const auto* viewport = services.viewport;
    const int uiW = viewport ? viewport->width : 1280;
    const int uiH = viewport ? viewport->height : 720;
    const int cardW = 144;
    const int cardH = 99;
    const int spacing = 16;
    const int margin = 40;
    const int y = std::max(0, uiH - cardH - margin);

    shopCardsY = y;
    shopCardsH = cardH;
    shopCardsValid = !cards.empty();
    shopCardSystem.spawnCardRowLayout(cards, uiW, y, cardW, cardH, spacing);
}

void CombatState::drawShopHud(int uiW, int uiH) {
    (void)uiH;
    if (!shopUiEnabled || !shopHudText || !shopCardsValid) return;

    const int money = gameWorld ? gameWorld->getMoney() : 0;
    const std::string moneyText = std::to_string(std::max(0, money));
    const float moneyScale = 1.35f;
    const float moneyW = shopHudText->measureTextWidth(moneyText, moneyScale);

    const bool showReroll = hasShopRerollButton;
    const std::string rerollLabel = "[Reroll 2g]";
    const float rerollScale = 0.92f;
    const float rerollW = showReroll ? shopHudText->measureTextWidth(rerollLabel, rerollScale) : 0.0f;
    const float gap = showReroll ? 18.0f : 0.0f;

    const float totalW = moneyW + gap + rerollW;
    const float x0 = std::round((static_cast<float>(uiW) - totalW) * 0.5f);
    const float y0 = std::max(6.0f, static_cast<float>(shopCardsY) - 46.0f);

    const glm::vec3 goldColor(1.0f, 0.92f, 0.10f);
    // Keep combat gold bright yellow (no dark shadow pass).
    shopHudText->renderText(moneyText, x0, y0, goldColor, moneyScale);
    shopHudText->renderText(moneyText, x0 + 0.75f, y0, goldColor, moneyScale);
    shopHudText->renderText(moneyText, x0, y0 + 0.75f, goldColor, moneyScale);

    if (showReroll) {
        shopRerollX = x0 + moneyW + gap;
        shopRerollY = y0 + 4.0f;
        shopRerollW = rerollW;
        shopRerollH = static_cast<float>(services.config.fontSize) * rerollScale;
        shopHudText->renderText(rerollLabel, shopRerollX, shopRerollY, glm::vec3(1.0f, 1.0f, 1.0f), rerollScale);
    } else {
        shopRerollX = 0.0f;
        shopRerollY = 0.0f;
        shopRerollW = 0.0f;
        shopRerollH = 0.0f;
    }
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

void CombatState::onEnter() {
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

    if (services.ecsWorld && services.ecsWorld->alive(services.combatStateEntity)) {
        if (auto* state = services.ecsWorld->get<game::CombatActive>(services.combatStateEntity)) {
            state->active = true;
        } else {
            services.ecsWorld->add<game::CombatActive>(services.combatStateEntity, game::CombatActive{true});
        }
    }

    sol::table S = script.getScriptTable();

    if (sol::function get_message = S["get_message"]; get_message.valid()) {
        if (auto r = get_message(); r.valid() && r.get_type() == sol::type::string) {
            combatMessage = r.get<std::string>();
        }
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
    if (services.ecsWorld && services.ecsWorld->alive(services.combatStateEntity)) {
        if (auto* state = services.ecsWorld->get<game::CombatActive>(services.combatStateEntity)) {
            state->active = false;
        }
    }
    if (gameWorld) {
        gameWorld->restorePlayerPositionsAfterBattle();
        gameWorld->setBoardInteractionLocked(false);
        gameWorld->healPlayerUnitsToFull();
        gameWorld->resetCombatBalance();
    }
    shopCardSystem.clearCards();
    shopCardsValid = false;
    shopUiEnabled = false;
    hasShopRerollButton = false;
    shopRerollX = 0.0f;
    shopRerollY = 0.0f;
    shopRerollW = 0.0f;
    shopRerollH = 0.0f;
    script.onExit();
}

void CombatState::handleInput(const InputEvent& event) {
    script.call("handleInput");
    if (event.type == InputEvent::Type::MouseDown && gameWorld) {
        if (gameWorld->consumeUiClickBlocked()) return;
        if (gameWorld->isUnitDragActive()) return;
    }
    if (!shopUiEnabled) return;
    if (event.type != InputEvent::Type::MouseDown) return;

    sol::table S = script.getScriptTable();

    if (hasShopRerollButton) {
        const bool insideX = static_cast<float>(event.mouseX) >= shopRerollX &&
                             static_cast<float>(event.mouseX) <= (shopRerollX + shopRerollW);
        const bool insideY = static_cast<float>(event.mouseY) >= shopRerollY &&
                             static_cast<float>(event.mouseY) <= (shopRerollY + shopRerollH);
        if (insideX && insideY) {
            sol::function onReroll = S["on_shop_reroll_click"];
            if (onReroll.valid()) onReroll();
            script.flushCommands();
            rebuildShopCards();
            return;
        }
    }

    auto clicked = shopCardSystem.handleMouseClick(event.mouseX, event.mouseY);
    if (!clicked) return;

    sol::function onClick = S["on_shop_card_click"];
    if (!onClick.valid()) onClick = S["on_card_click"];
    if (!onClick.valid()) onClick = S["onCardClick"];
    if (onClick.valid()) {
        onClick(clicked->pokemonName, clicked->level);
        script.flushCommands();
        rebuildShopCards();
    }
}

void CombatState::update(float dt) {
    script.onUpdate(dt);
}

void CombatState::render() {
    if (!textRenderer) {
        const auto& c = services.config;
        textRenderer = std::make_unique<TextRenderer>(c.fontPath, c.fontSize);
    }
    if (!textRenderer) return;

    const float scale = 1.0f;
    const std::string msg = combatMessage.empty() ? std::string("Combat") : combatMessage;

    const auto* viewport = services.viewport;
    const float uiWidth = static_cast<float>(viewport ? viewport->width : 1280);
    float textWidth = textRenderer->measureTextWidth(msg, scale);
    float centeredX = viewport ? viewport->centerX(textWidth)
                               : std::round((uiWidth - textWidth) * 0.5f);

    textRenderer->renderText(msg, centeredX, 22.0f, glm::vec3(1.0f), scale);

    if (shopUiEnabled) {
        const int uiW = viewport ? viewport->width : 1280;
        const int uiH = viewport ? viewport->height : 720;
        const bool showSellOverlay = gameWorld &&
                                     gameWorld->isUnitDragActive() &&
                                     !gameWorld->getClassicShopCards().empty();
        if (!showSellOverlay) {
            shopCardSystem.render(uiW, uiH);
        } else if (shopHudText) {
            const std::string sellLabel = "[ DROP HERE TO SELL ]";
            const float sellScale = 1.0f;
            const float labelW = shopHudText->measureTextWidth(sellLabel, sellScale);
            const float x = std::round((static_cast<float>(uiW) - labelW) * 0.5f);
            const float y = std::round(static_cast<float>(shopCardsY) + static_cast<float>(shopCardsH) * 0.5f);
            shopHudText->renderText(sellLabel, x, y, glm::vec3(1.0f, 0.35f, 0.35f), sellScale);
        }
        drawShopHud(uiW, uiH);
    }
}
