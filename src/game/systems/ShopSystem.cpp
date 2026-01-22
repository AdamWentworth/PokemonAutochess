// src/game/systems/ShopSystem.cpp
#include "ShopSystem.h"

#include <SDL2/SDL.h>
#include <random>

ShopSystem::ShopSystem()
    : rng(std::random_device{}())
{
    // Temporary placeholder pool (replace with real data later)
    fallbackPool = {
        "pikachu",
        "charmander",
        "squirtle",
        "bulbasaur",
        "eevee",
        "jigglypuff",
        "meowth",
        "psyduck",
        "snorlax"
    };

    cardSystem.init();
}

void ShopSystem::update(float dt) {
    (void)dt;
    // No simulation here yet
}

void ShopSystem::setVisible(bool v) {
    isVisible = v;

    if (!isVisible) {
        cardSystem.clearCards();
        currentCards.clear();
    }
}

void ShopSystem::onRoundPhaseChanged(RoundPhase previous, RoundPhase next) {
    (void)previous;

    // Show shop during Planning, hide otherwise
    if (next == RoundPhase::Planning) {
        setVisible(false);
        rollShop();
    } else {
        setVisible(false);
    }
}

void ShopSystem::rollShop() {
    if (fallbackPool.empty()) {
        SDL_Log("[ShopSystem] fallbackPool is empty; nothing to roll.");
        currentCards.clear();
        cardSystem.clearCards();
        return;
    }

    // Pick 5 random entries from the pool (with replacement for now).
    constexpr int kShopSize = 5;
    std::uniform_int_distribution<int> dist(0, static_cast<int>(fallbackPool.size()) - 1);

    currentCards.clear();
    currentCards.reserve(kShopSize);

    for (int i = 0; i < kShopSize; ++i) {
        CardData data;
        data.pokemonName = fallbackPool[dist(rng)];
        data.cost = 1; // placeholder
        data.type = CardType::Shop;
        currentCards.push_back(std::move(data));
    }

    // Spawn cards near bottom of screen.
    // TODO: replace hardcoded 1280 with your real drawable width (or pass width in).
    cardSystem.spawnCardRow(currentCards, /*screenWidth*/ 1280, /*yOffset*/ 520);
}

void ShopSystem::handleInput(const InputEvent& event) {
    if (!isVisible) return;

    if (event.type == InputEvent::Type::MouseDown) {
        // Left mouse button is 1 (SDL convention)
        if (event.mouseButton == 1) {
            // CardSystem expects screen-space pixel coords
            if (auto clicked = cardSystem.handleMouseClick(event.mouseX, event.mouseY)) {
                SDL_Log("[ShopSystem] Selected: %s (cost=%d)",
                    clicked->pokemonName.c_str(), clicked->cost);
            }
        }
    }
}

void ShopSystem::renderUI(int screenWidth, int screenHeight) {
    if (!isVisible) return;
    cardSystem.render(screenWidth, screenHeight);
}
