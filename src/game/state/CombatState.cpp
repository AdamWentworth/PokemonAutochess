#include "CombatState.h"

#include "game/GameWorld.h"
#include "game/GameServices.h"
#include "game/logging/LoggerUtil.h"
#include "game/ui/UIViewport.h"

#include "game/ecs/CombatActive.h"
#include "engine/core/ecs/World.h"
#include "engine/input/InputEvent.h"

#include "engine/ui/UIManager.h"
#include "engine/ui/TextRenderer.h"
#include "engine/utils/Shader.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sol/sol.hpp>

namespace {
std::string Capitalize(std::string s) {
    if (s.empty()) return s;
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

struct ShopRowLayout {
    int cardW = 136;
    int cardH = 94;
    int spacing = 14;
    int edgeMargin = 18;
};

ShopRowLayout computeShopRowLayout(int uiW, int uiH, bool allItems) {
    const float scale = std::clamp(
        std::min(static_cast<float>(uiW) / 1280.0f,
                 static_cast<float>(uiH) / 720.0f),
        0.60f, 1.80f);

    const int baseW = allItems ? 88 : 136;
    const int baseH = allItems ? 88 : 94;
    const int baseSpacing = allItems ? 12 : 14;

    ShopRowLayout out;
    out.cardW = std::max(56, static_cast<int>(std::round(static_cast<float>(baseW) * scale)));
    out.cardH = std::max(56, static_cast<int>(std::round(static_cast<float>(baseH) * scale)));
    out.spacing = std::max(6, static_cast<int>(std::round(static_cast<float>(baseSpacing) * scale)));
    out.edgeMargin = std::clamp(
        static_cast<int>(std::round(std::min(uiW, uiH) * 0.024f)),
        10, 36);
    return out;
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
        shopCardsX = 0;
        shopCardsValid = false;
        shopRerollX = 0.0f;
        shopRerollY = 0.0f;
        shopRerollW = 0.0f;
        shopRerollH = 0.0f;
        releaseCurrencyHudResources();
        return;
    }

    if (!shopUiInitialized) {
        shopCardSystem.init();
        shopCardSystem.initOverlayText(services.config.fontPath, std::max(16, services.config.fontSize / 2));
        shopHudText = std::make_unique<TextRenderer>(services.config.fontPath, std::max(28, services.config.fontSize / 2));
        shopUiInitialized = true;
    }

    ensureCurrencyHudResources();
    rebuildShopCards();
}

void CombatState::ensureCurrencyHudResources() {
    if (!services.renderEnabled) return;

    const std::string desiredIconPath = (services.gameMode == "adventure")
        ? "assets/images/pokedollar.png"
        : "assets/images/pokegold.png";
    if (desiredIconPath != currencyIconPath) {
        if (currencyIconTexture != 0) {
            glDeleteTextures(1, &currencyIconTexture);
            currencyIconTexture = 0;
        }
        currencyIconPath = desiredIconPath;
        currencyIconTexture = loadCurrencyTexture(currencyIconPath);
    }

    if (currencyVAO != 0) return;

    const float vertices[] = {
        // pos      // uv
        0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f
    };
    const unsigned int indices[] = { 0, 1, 2, 2, 3, 0 };

    glGenVertexArrays(1, &currencyVAO);
    glGenBuffers(1, &currencyVBO);
    glGenBuffers(1, &currencyEBO);

    glBindVertexArray(currencyVAO);
    glBindBuffer(GL_ARRAY_BUFFER, currencyVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, currencyEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void CombatState::releaseCurrencyHudResources() {
    if (currencyEBO != 0) {
        glDeleteBuffers(1, &currencyEBO);
        currencyEBO = 0;
    }
    if (currencyVBO != 0) {
        glDeleteBuffers(1, &currencyVBO);
        currencyVBO = 0;
    }
    if (currencyVAO != 0) {
        glDeleteVertexArrays(1, &currencyVAO);
        currencyVAO = 0;
    }
    if (currencyIconTexture != 0) {
        glDeleteTextures(1, &currencyIconTexture);
        currencyIconTexture = 0;
    }
    currencyIconPath.clear();
}

unsigned int CombatState::loadCurrencyTexture(const std::string& path) const {
    int w = 0, h = 0, channels = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) {
        std::cerr << "[CombatState] Failed to load currency icon: " << path << "\n";
        return 0;
    }

    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return tex;
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
    bool allItems = !cards.empty();
    for (const auto& cd : cards) {
        if (cd.type != CardType::Item) {
            allItems = false;
            break;
        }
    }
    const ShopRowLayout layout = computeShopRowLayout(uiW, uiH, allItems);
    const int cardW = layout.cardW;
    const int cardH = layout.cardH;
    const int spacing = layout.spacing;
    const int y = std::max(0, uiH - cardH - layout.edgeMargin);
    const int count = static_cast<int>(cards.size());
    const int totalWidth = (count > 0) ? (count * cardW + (count - 1) * spacing) : 0;
    const int startX = (uiW - totalWidth) / 2;

    shopCardsX = startX;
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
    const float moneyH = shopHudText->measureTextHeight(moneyText, moneyScale);
    const float moneyW = shopHudText->measureTextWidth(moneyText, moneyScale);

    const bool showReroll = hasShopRerollButton;
    const std::string rerollLabel = "[Reroll 2g]";
    const float rerollScale = 0.92f;
    const float rerollH = shopHudText->measureTextHeight(rerollLabel, rerollScale);
    const float rerollW = showReroll ? shopHudText->measureTextWidth(rerollLabel, rerollScale) : 0.0f;
    const float iconSize = (services.gameMode == "adventure") ? 34.0f : 30.0f;
    const float iconGap = (currencyIconTexture != 0) ? 8.0f : 0.0f;
    const float edgePad = std::clamp(
        std::round(static_cast<float>(std::min(uiW, uiH)) * 0.02f),
        12.0f, 28.0f);
    const float topRowWidth = moneyW + ((currencyIconTexture != 0) ? (iconSize + iconGap) : 0.0f);
    const float blockWidth = std::max(topRowWidth, showReroll ? rerollW : 0.0f);
    const float adjacentGap = 10.0f;
    const float maxX = std::max(edgePad, static_cast<float>(uiW) - blockWidth - edgePad);
    const float desiredX = static_cast<float>(shopCardsX) - blockWidth - adjacentGap;
    const float x0 = std::clamp(desiredX, edgePad, maxX);
    const float topRowH = std::max(moneyH, iconSize);
    const float stackGap = showReroll ? 12.0f : 0.0f;
    const float cardBottom = static_cast<float>(shopCardsY + shopCardsH);
    const float y0 = showReroll
        ? (cardBottom - rerollH - stackGap - topRowH)
        : (cardBottom - topRowH);

    if (currencyIconTexture != 0 && currencyVAO != 0) {
        Shader* shader = UIManager::getCardShader();
        if (shader) {
            const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
            if (depthWasEnabled) glDisable(GL_DEPTH_TEST);
            const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
            if (!blendWasEnabled) glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            const glm::mat4 projection = glm::ortho(
                0.0f, static_cast<float>(uiW),
                static_cast<float>(uiH), 0.0f);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x0, y0, 0.0f));
            model = glm::scale(model, glm::vec3(iconSize, iconSize, 1.0f));

            shader->use();
            shader->setUniform("u_Projection", projection);
            shader->setUniform("u_Model", model);
            shader->setUniform("u_UVMin", glm::vec2(0.0f, 0.0f));
            shader->setUniform("u_UVMax", glm::vec2(1.0f, 1.0f));
            shader->setUniform("u_Texture", 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, currencyIconTexture);
            glBindVertexArray(currencyVAO);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glUseProgram(0);

            if (!blendWasEnabled) glDisable(GL_BLEND);
            if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
        }
    }

    const glm::vec3 goldColor(1.0f, 0.92f, 0.10f);
    const float textX = x0 + ((currencyIconTexture != 0) ? (iconSize + iconGap) : 0.0f);
    const float textY = y0;
    // Keep combat gold bright yellow (no dark shadow pass).
    shopHudText->renderText(moneyText, textX, textY, goldColor, moneyScale);
    shopHudText->renderText(moneyText, textX + 0.75f, textY, goldColor, moneyScale);
    shopHudText->renderText(moneyText, textX, textY + 0.75f, goldColor, moneyScale);

    if (showReroll) {
        shopRerollX = x0;
        shopRerollY = cardBottom - rerollH;
        shopRerollW = rerollW;
        shopRerollH = rerollH;
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

CombatState::~CombatState() {
    releaseCurrencyHudResources();
}

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
    shopCardsX = 0;
    shopCardsValid = false;
    shopUiEnabled = false;
    hasShopRerollButton = false;
    shopRerollX = 0.0f;
    shopRerollY = 0.0f;
    shopRerollW = 0.0f;
    shopRerollH = 0.0f;
    releaseCurrencyHudResources();
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
