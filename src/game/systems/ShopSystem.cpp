// ShopSystem.cpp
#include "game/systems/ShopSystem.h"

#include <iostream>

ShopSystem::ShopSystem() {
    rng.seed(std::random_device{}());

    // Minimal placeholder pool (replace with real odds/config later)
    fallbackPool = {"bulbasaur", "charmander", "squirtle", "pidgey", "rattata"};
}

void ShopSystem::update(float dt) {
    (void)dt;
    // No-op; ShopSystem is event/phase-driven right now.
}

void ShopSystem::setVisible(bool v) {
    if (v == isVisible) return;
    isVisible = v;

    // TEMP: shop UI disabled (no cards rendered/spawned yet)
    currentCards.clear();
    cardSystem.clearCards();
}

void ShopSystem::onRoundPhaseChanged(RoundPhase previous, RoundPhase next) {
    (void)previous;

    // Show shop during Planning phase only
    setVisible(next == RoundPhase::Planning);
}

void ShopSystem::rollShop() {
    // TEMP: disabled until card UI is ready
    currentCards.clear();
    cardSystem.clearCards();
}

void ShopSystem::handleInput(const InputEvent& event) {
    (void)event;
    // TEMP: disabled until card UI is ready
}

void ShopSystem::renderUI(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    // TEMP: disabled until card UI is ready
}
