// StarterSelectionState.cpp
#include "StarterSelectionState.h"
#include "../GameStateManager.h"
#include "../GameWorld.h"
#include "PlacementState.h"
#include "../../engine/ui/Card.h"
#include "../../engine/ui/UIManager.h"
#include "../../engine/utils/Shader.h"
#include "../../engine/ui/TextRenderer.h"
#include "../ui/CardFactory.h"
#include "../GameConfig.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <sol/sol.hpp>

static TextRenderer* textRenderer = nullptr;

StarterSelectionState::StarterSelectionState(GameStateManager* manager, GameWorld* world)
    : stateManager(manager),
      gameWorld(world),
      selectedStarter(StarterPokemon::None),
      script(world)
{
    // Base state script (keeps onCardClick behavior)
    script.loadScript("scripts/states/starter_selection.lua");

    cardSystem.init();

    // NEW: Load starter menu data from Lua and create cards
    sol::state& L = script.getState();
    sol::load_result menuChunk = L.load_file("scripts/ui/starter_menu.lua");
    if (menuChunk.valid()) {
        sol::protected_function_result _ = menuChunk();
    } else {
        sol::error e = menuChunk;
        std::cerr << "[StarterSelectionState] Failed to load starter_menu.lua: " << e.what() << "\n";
    }

    sol::function get_cards = L["get_starter_cards"];
    if (get_cards.valid()) {
        std::vector<CardData> starters;
        sol::table t = get_cards();
        for (auto& kv : t) {
            sol::table row = kv.second.as<sol::table>();
            CardData d;
            d.pokemonName = row["name"].get<std::string>();
            d.cost = row.get_or("cost", 0);
            d.type = CardType::Starter;
            starters.push_back(d);
        }
        auto starterCards = CardFactory::createCardRow(starters, 1280, 300);
        for (auto& card : starterCards) {
            cardSystem.addCard(std::move(card));
        }
    }
}

StarterSelectionState::~StarterSelectionState() {}

void StarterSelectionState::onEnter() {
    std::cout << "[StarterSelectionState] Entering starter selection.\n";
    script.onEnter();

    if (!textRenderer) {
        const auto& cfg = GameConfig::get();
        textRenderer = new TextRenderer(cfg.fontPath, cfg.fontSize);
    }
}

void StarterSelectionState::onExit() {
    std::cout << "[StarterSelectionState] Exiting starter selection.\n";
    script.onExit();
}

void StarterSelectionState::handleInput(SDL_Event& event) {
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        int mouseX = event.button.x;
        int mouseY = event.button.y;

        auto clicked = cardSystem.handleMouseClick(mouseX, mouseY);
        if (clicked.has_value()) {
            const std::string& name = clicked->pokemonName;
            script.call("onCardClick", name);
            stateManager->pushState(std::make_unique<PlacementState>(stateManager, gameWorld, name));
        }
    }

    if (event.type == SDL_KEYDOWN) {
        // Ask Lua which starter a key maps to
        sol::state& L = script.getState();
        sol::function handle_key = L["handle_starter_key"];
        if (handle_key.valid()) {
            std::string keyname;
            switch (event.key.keysym.sym) {
                case SDLK_1: keyname = "1"; break;
                case SDLK_2: keyname = "2"; break;
                case SDLK_3: keyname = "3"; break;
                default: break;
            }
            if (!keyname.empty()) {
                sol::object res = handle_key(keyname);
                if (res.is<std::string>()) {
                    std::string pick = res.as<std::string>();
                    script.call("onCardClick", pick);
                    stateManager->pushState(std::make_unique<PlacementState>(stateManager, gameWorld, pick));
                }
            }
        }
    }
}

void StarterSelectionState::update(float deltaTime) {
    script.onUpdate(deltaTime);
    cardSystem.update(deltaTime);
}

void StarterSelectionState::render() {
    const std::string message = "CHOOSE YOUR STARTER";
    const float scale = 1.0f;
    const int windowWidth = 1280;

    float textWidth = textRenderer->measureTextWidth(message, scale);
    float centeredX = std::round((windowWidth - textWidth) / 2.0f);
    float textY = 150.0f;

    textRenderer->renderText(message, centeredX, textY, glm::vec3(1.0f), scale);
    cardSystem.render(1280, 720);
}

bool StarterSelectionState::isPointInRect(int x, int y, const SDL_Rect& rect) const {
    return (x >= rect.x && x <= rect.x + rect.w &&
            y >= rect.y && y <= rect.y + rect.h);
}
