#include "ScriptedState.h"

#include "game/GameStateManager.h"
#include "game/GameServices.h"
#include "game/state/PlacementState.h"
#include "game/ui/UIViewport.h"
#include "engine/input/InputEvent.h"
#include "engine/ui/UIManager.h"
#include "engine/utils/Shader.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>

#include <sol/sol.hpp>
#include <algorithm>
#include <iostream>

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

ScriptedState::~ScriptedState() {
    releaseCurrencyHudResources();
}

void ScriptedState::rebuildCardRow() {
    sol::table S = script.getScriptTable();
    sol::protected_function f;

    auto buildList = [&](sol::protected_function fn, std::vector<CardData>& out) {
        out.clear();
        if (!fn.valid()) return false;

        sol::protected_function_result r = fn();
        if (!(r.valid() && r.get_type() == sol::type::table)) {
            std::cerr << "[ScriptedState] card list function did not return a table\n";
            return false;
        }

        sol::table t = r;
        for (auto&& kv : t) {
            sol::table row = kv.second.as<sol::table>();
            CardData cd;

            auto nameOpt = row.get<sol::optional<std::string>>("name");
            cd.pokemonName = nameOpt.value_or(std::string());

            auto costOpt = row.get<sol::optional<int>>("cost");
            cd.cost = costOpt.value_or(0);

            auto levelOpt = row.get<sol::optional<int>>("level");
            cd.level = levelOpt.value_or(0);

            auto labelOpt = row.get<sol::optional<std::string>>("label");
            cd.label = labelOpt.value_or(std::string());

            auto imageOpt = row.get<sol::optional<std::string>>("image");
            cd.imagePath = imageOpt.value_or(std::string());

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

            if (!cd.pokemonName.empty() || !cd.label.empty()) out.push_back(cd);
        }
        return true;
    };

    std::vector<CardData> list;
    if (cardMode == CardMode::Shop) {
        f = S["get_shop_cards"];
    } else if (cardMode == CardMode::Starter) {
        f = S["get_starter_cards"];
    }
    if (!buildList(f, list)) return;

    const auto* viewport = services.viewport;
    const int uiW = viewport ? viewport->width : 1280;
    const int uiH = viewport ? viewport->height : 720;
    if (cardMode == CardMode::Shop) {
        bool allItems = !list.empty();
        for (const auto& cd : list) {
            if (cd.type != CardType::Item) { allItems = false; break; }
        }

        const int cardW = allItems ? 96 : 160;
        const int cardH = allItems ? 96 : 110;
        const int spacing = allItems ? 28 : 20;
        const int margin = 40;
        const int y = std::max(0, uiH - cardH - margin);
        shopCardsY = y;
        shopCardsH = cardH;
        shopCardsValid = !list.empty();
        cardSystem.spawnCardRowLayout(list, uiW, y, cardW, cardH, spacing);
        if (hasShopItems) {
            sol::protected_function itemFn = S["get_shop_items"];
            std::vector<CardData> items;
            if (buildList(itemFn, items)) {
                const int itemW = 96;
                const int itemH = 96;
                const int itemSpacing = 14;
                const int itemMargin = 32;
                const int itemY = std::max(0, itemMargin);
                itemCardSystem.spawnCardRowLayout(items, uiW, itemY, itemW, itemH, itemSpacing);
            }
        }
    } else {
        shopCardsValid = false;
        cardSystem.spawnCardRow(list, uiW, /*y*/ 300);
    }
    std::cout << "[ScriptedState] Spawned " << list.size() << " cards\n";
}

void ScriptedState::rebuildTextMenu() {
    textMenuEntries.clear();

    sol::table S = script.getScriptTable();
    sol::protected_function f = S["get_text_menu_entries"];
    if (!f.valid()) return;

    sol::protected_function_result r = f();
    if (!(r.valid() && r.get_type() == sol::type::table)) {
        std::cerr << "[ScriptedState] text menu function did not return a table\n";
        return;
    }

    sol::table t = r;
    for (auto&& kv : t) {
        if (kv.second.get_type() != sol::type::table) continue;
        sol::table row = kv.second.as<sol::table>();

        TextMenuEntry entry;
        auto idOpt = row.get<sol::optional<std::string>>("id");
        auto nameOpt = row.get<sol::optional<std::string>>("name");
        auto labelOpt = row.get<sol::optional<std::string>>("label");

        entry.id = idOpt.value_or(nameOpt.value_or(std::string()));
        entry.label = labelOpt.value_or(entry.id);
        if (entry.id.empty()) entry.id = entry.label;
        if (entry.id.empty() || entry.label.empty()) continue;

        textMenuEntries.push_back(std::move(entry));
    }
}

void ScriptedState::ensureCardUI() {
    if (uiInitialized) return;
    if (!services.renderEnabled) {
        cardMode = CardMode::None;
        shopCardsValid = false;
        hasShopReadyButton = false;
        uiInitialized = true;
        return;
    }

    // Script functions/vars live in the script environment now.
    sol::table S = script.getScriptTable();

    bool hasTextMenuEntries = S["get_text_menu_entries"].valid();
    bool hasTextMenuClick = S["on_text_menu_click"].valid() || S["on_menu_click"].valid();
    bool hasShopCards = S["get_shop_cards"].valid();
    bool hasShopClick = S["on_shop_card_click"].valid() || S["on_card_click"].valid() || S["onCardClick"].valid();
    bool hasStarterCards = S["get_starter_cards"].valid();
    bool hasStarterClick = S["on_card_click"].valid() || S["onCardClick"].valid();
    hasShopItems = S["get_shop_items"].valid() && S["on_shop_item_click"].valid();
    hasShopReadyButton = S["on_shop_ready_click"].valid();
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
        uiInitialized = true;
        return;
    }

    const auto& c = services.config;
    if (cardMode != CardMode::TextMenu) {
        cardSystem.init();
        cardSystem.initOverlayText(c.fontPath, std::max(14, c.fontSize / 3));
        if (hasShopItems) {
            itemCardSystem.init();
            itemCardSystem.initOverlayText(c.fontPath, std::max(14, c.fontSize / 3));
        }
        if (cardMode == CardMode::Shop) {
            ensureCurrencyHudResources();
        }
    } else {
        cardSystem.clearCards();
        itemCardSystem.clearCards();
        shopCardsValid = false;
        hasShopReadyButton = false;
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
    script.onExit();
}

void ScriptedState::handleInput(const InputEvent& event) {
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
        cardMode = CardMode::None;
        renderWorld = true;
        hasShopReadyButton = false;
        ensureCardUI();
        return; // avoid also sending this key into old script state
    }

    // If your scripts expect the event, you can add bindings later; keep current behavior.
    script.call("handleInput");

    if (!uiInitialized) return;

    sol::table S = script.getScriptTable();

    if (event.type == InputEvent::Type::MouseDown) {
        if (cardMode == CardMode::TextMenu) {
            for (const auto& entry : textMenuEntries) {
                const bool insideX = static_cast<float>(event.mouseX) >= entry.x &&
                                     static_cast<float>(event.mouseX) <= (entry.x + entry.w);
                const bool insideY = static_cast<float>(event.mouseY) >= entry.y &&
                                     static_cast<float>(event.mouseY) <= (entry.y + entry.h);
                if (!(insideX && insideY)) continue;

                sol::function onMenuClick = S["on_text_menu_click"];
                if (!onMenuClick.valid()) onMenuClick = S["on_menu_click"];
                if (onMenuClick.valid()) {
                    onMenuClick(entry.id);
                }
                script.flushCommands();
                rebuildTextMenu();
                return;
            }
        }

        if (cardMode == CardMode::Shop && hasShopReadyButton) {
            const bool insideReadyX = static_cast<float>(event.mouseX) >= shopReadyX &&
                                      static_cast<float>(event.mouseX) <= (shopReadyX + shopReadyW);
            const bool insideReadyY = static_cast<float>(event.mouseY) >= shopReadyY &&
                                      static_cast<float>(event.mouseY) <= (shopReadyY + shopReadyH);
            if (insideReadyX && insideReadyY) {
                sol::function onReady = S["on_shop_ready_click"];
                if (onReady.valid()) {
                    onReady();
                }
                script.flushCommands();
                rebuildCardRow();
                return;
            }
        }

        auto clicked = cardSystem.handleMouseClick(event.mouseX, event.mouseY);
        if (clicked) {
            if (cardMode == CardMode::Shop) {
                sol::function onClick = S["on_shop_card_click"];
                if (!onClick.valid()) onClick = S["on_card_click"];
                if (!onClick.valid()) onClick = S["onCardClick"];
                if (onClick.valid()) {
                    onClick(clicked->pokemonName, clicked->level);
                }
                script.flushCommands();
                rebuildCardRow();
            } else if (cardMode == CardMode::Starter) {
                sol::function onClick = S["on_card_click"];
                if (!onClick.valid()) onClick = S["onCardClick"];
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
                sol::function onItemClick = S["on_shop_item_click"];
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

                        sol::function onClick = S["on_card_click"];
                        if (!onClick.valid()) onClick = S["onCardClick"];
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

    sol::function getMsg = S["get_message"];
    if (getMsg.valid() && titleText) {
        sol::protected_function_result r = getMsg();
        if (r.valid() && r.get_type() == sol::type::string) {
            std::string msg = r.get<std::string>();
            float w = titleText->measureTextWidth(msg, 1.0f);
            float x = (static_cast<float>(uiW) - w) * 0.5f;
            const glm::vec3 msgColor = (cardMode == CardMode::TextMenu)
                ? glm::vec3(1.0f, 1.0f, 1.0f)
                : glm::vec3(1.0f, 1.0f, 0.0f);
            titleText->renderText(msg, x, 150.0f, msgColor, 1.0f);
        }
    }

    if (cardMode == CardMode::Shop && hasShopReadyButton && titleText) {
        const std::string readyLabel = "[ Ready ]";
        const float readyScale = 0.9f;
        shopReadyW = titleText->measureTextWidth(readyLabel, readyScale);
        shopReadyH = static_cast<float>(services.config.fontSize) * readyScale;
        shopReadyX = static_cast<float>(uiW) - shopReadyW - 36.0f;
        shopReadyY = 152.0f;
        titleText->renderText(readyLabel, shopReadyX + 1.0f, shopReadyY + 1.0f,
                              glm::vec3(0.0f, 0.0f, 0.0f), readyScale, 0.65f);
        titleText->renderText(readyLabel, shopReadyX, shopReadyY,
                              glm::vec3(0.95f, 0.95f, 0.95f), readyScale);
    } else {
        shopReadyW = 0.0f;
        shopReadyH = 0.0f;
    }

    if (cardMode == CardMode::TextMenu && titleText) {
        const float menuScale = 1.0f;
        const float textH = static_cast<float>(services.config.fontSize) * menuScale;
        const float startY = 220.0f;
        const float spacing = textH + 16.0f;

        for (size_t i = 0; i < textMenuEntries.size(); ++i) {
            auto& entry = textMenuEntries[i];
            entry.w = titleText->measureTextWidth(entry.label, menuScale);
            entry.h = textH;
            entry.x = (static_cast<float>(uiW) - entry.w) * 0.5f;
            entry.y = startY + static_cast<float>(i) * spacing;

            titleText->renderText(entry.label, entry.x, entry.y,
                                  {1.0f, 1.0f, 1.0f}, menuScale);
        }
        return;
    }

    cardSystem.render(uiW, uiH);
    if (cardMode == CardMode::Shop && hasShopItems) {
        itemCardSystem.render(uiW, uiH);
    }
    if (cardMode == CardMode::Shop) {
        drawCurrencyHud(uiW, uiH);
    }
}

void ScriptedState::ensureCurrencyHudResources() {
    if (!services.renderEnabled) return;

    if (!currencyText) {
        currencyText = std::make_unique<TextRenderer>(
            services.config.fontPath,
            std::max(16, services.config.fontSize / 3));
    }

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

void ScriptedState::releaseCurrencyHudResources() {
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
    currencyText.reset();
}

unsigned int ScriptedState::loadCurrencyTexture(const std::string& path) const {
    int w = 0, h = 0, channels = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) {
        std::cerr << "[ScriptedState] Failed to load currency icon: " << path << "\n";
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

void ScriptedState::drawCurrencyHud(int uiW, int uiH) {
    (void)uiH;
    if (!currencyText || !shopCardsValid) return;

    const int money = gameWorld ? gameWorld->getMoney() : 0;
    const std::string moneyText = std::to_string(std::max(0, money));
    const float textScale = 0.9f;
    const float textWidth = currencyText->measureTextWidth(moneyText, textScale);
    const float iconSize = (services.gameMode == "adventure") ? 34.0f : 30.0f;
    const float gap = (currencyIconTexture != 0) ? 8.0f : 0.0f;
    const float totalWidth = textWidth + ((currencyIconTexture != 0) ? (iconSize + gap) : 0.0f);
    const float x0 = std::round((static_cast<float>(uiW) - totalWidth) * 0.5f);
    const float y0 = std::max(6.0f, static_cast<float>(shopCardsY) - iconSize - 10.0f);

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

    const float textX = x0 + ((currencyIconTexture != 0) ? (iconSize + gap) : 0.0f);
    const float textY = y0 + 3.0f;
    currencyText->renderText(moneyText, textX, textY, glm::vec3(1.0f, 0.92f, 0.10f), textScale);
}
