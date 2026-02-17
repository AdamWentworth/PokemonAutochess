#include "ScriptedState.h"

#include "game/GameStateManager.h"
#include "game/GameServices.h"
#include "game/scripting/LuaCardParser.h"
#include "game/scripting/LuaScriptHelpers.h"
#include "game/scripting/LuaTextMenuParser.h"
#include "game/state/PlacementState.h"
#include "game/ui/ShopLayout.h"
#include "game/ui/UIViewport.h"
#include "engine/input/InputEvent.h"
#include "engine/render/IRenderBackend.h"

#include <sol/sol.hpp>
#include <stb_easy_font.h>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
struct EasyFontVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    unsigned char color[4] = {255, 255, 255, 255};
};
static_assert(sizeof(EasyFontVertex) == 16, "Unexpected stb_easy_font vertex layout.");

constexpr float kBackendTextScaleBase = 2.0f;

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

void appendEasyFontTextQuads(std::vector<IRenderBackend::DebugQuad>& out,
                             float originX,
                             float originY,
                             const std::string& text,
                             float scale,
                             float r,
                             float g,
                             float b,
                             float a) {
    if (text.empty()) return;

    const std::size_t approxBytes = text.size() * 320u + 4096u;
    const std::size_t vertexCount = std::max<std::size_t>(256u, approxBytes / sizeof(EasyFontVertex));
    std::vector<EasyFontVertex> verts(vertexCount);

    const int quadCount = stb_easy_font_print(
        originX,
        originY,
        const_cast<char*>(text.c_str()),
        nullptr,
        verts.data(),
        static_cast<int>(verts.size() * sizeof(EasyFontVertex)));
    if (quadCount <= 0) return;

    out.reserve(out.size() + static_cast<std::size_t>(quadCount));
    const std::size_t maxQuads = std::min<std::size_t>(
        static_cast<std::size_t>(quadCount),
        verts.size() / 4u);

    for (std::size_t i = 0; i < maxQuads; ++i) {
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        for (int v = 0; v < 4; ++v) {
            float x = verts[i * 4u + static_cast<std::size_t>(v)].x;
            float y = verts[i * 4u + static_cast<std::size_t>(v)].y;
            if (scale != 1.0f) {
                x = originX + (x - originX) * scale;
                y = originY + (y - originY) * scale;
            }
            if (v == 0) {
                minX = maxX = x;
                minY = maxY = y;
            } else {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }

        IRenderBackend::DebugQuad q;
        q.x = minX;
        q.y = minY;
        q.w = std::max(0.0f, maxX - minX);
        q.h = std::max(0.0f, maxY - minY);
        if (q.w <= 0.0f || q.h <= 0.0f) continue;
        q.r = r;
        q.g = g;
        q.b = b;
        q.a = a;
        out.push_back(q);
    }
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
        cardSystem.spawnCardRow(list, uiW, /*y*/ 300);
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
    const float autoStartY = std::max(150.0f, static_cast<float>(uiH) * 0.28f);
    const float rowGap = std::max(10.0f, static_cast<float>(uiH) * 0.02f);
    float autoY = autoStartY;

    int keyboardIndex = 0;
    for (auto& entry : textMenuEntries) {
        std::string display = entry.label;
        if (entry.enabled) {
            ++keyboardIndex;
            if (keyboardIndex <= 9) {
                display = "[" + std::to_string(keyboardIndex) + "] " + display;
            }
        }

        const float textScale = std::max(0.1f, entry.scale) * kBackendTextScaleBase;
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
    }
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

        const float textScale = std::max(0.1f, entry.scale) * kBackendTextScaleBase;
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

        appendEasyFontTextQuads(quads, entry.x, entry.y, display, textScale, tr, tg, tb, 1.0f);

        minX = std::min(minX, bg.x);
        minY = std::min(minY, bg.y);
        maxX = std::max(maxX, bg.x + bg.w);
        maxY = std::max(maxY, bg.y + bg.h);
        hasAnyEntry = true;
    }

    if (hasAnyEntry) {
        const auto msgOpt = game::scripting::callStringFunction(script.getScriptTable(), {"get_message"});
        std::string header = msgOpt ? *msgOpt : "Menu";
        appendEasyFontTextQuads(quads,
                                minX,
                                std::max(24.0f, minY - 62.0f),
                                header,
                                3.0f,
                                0.97f,
                                0.97f,
                                0.98f,
                                1.0f);
        appendEasyFontTextQuads(quads,
                                minX,
                                std::max(52.0f, minY - 30.0f),
                                "Click entries or press 1-9",
                                1.5f,
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
    std::cout << "[Menu][BackendUI] Non-OpenGL menu path active.\n";
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
        hasTextMenu = hasTextMenuEntries && hasTextMenuClick;

        renderWorld = true;
        if (auto hideWorld = S.get<sol::optional<bool>>("hide_world")) {
            renderWorld = !(*hideWorld);
        }
        if (hasTextMenu) {
            cardMode = CardMode::TextMenu;
            rebuildTextMenu();
            logHeadlessTextMenuHints();
        } else {
            cardMode = CardMode::None;
        }

        hasShopReadyButton = false;
        hasShopRerollButton = false;
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
        ensureCardUI();
        return; // avoid also sending this key into old script state
    }

    // If your scripts expect the event, you can add bindings later; keep current behavior.
    script.call("handleInput");

    if (!uiInitialized) return;
    if (cardMode == CardMode::TextMenu &&
        event.type == InputEvent::Type::KeyDown &&
        !event.repeat) {
        if (tryHandleHeadlessTextMenuKey(event.keyId)) {
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

    const bool showSellOverlay = (cardMode == CardMode::Shop) &&
                                 gameWorld &&
                                 gameWorld->isUnitDragActive() &&
                                 (gameWorld->getUnitDropZoneCardCount() > 0);

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
    in.showSellOverlay = gameWorld &&
                         gameWorld->isUnitDragActive() &&
                         (gameWorld->getUnitDropZoneCardCount() > 0);
    in.sellOverlayPaysMoney = gameWorld ? gameWorld->isUnitSellRewardsEnabled() : true;
    shopUi->render(in);
}
