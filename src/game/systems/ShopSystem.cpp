// ShopSystem.cpp
#include "game/systems/ShopSystem.h"

#include "engine/core/Random.h"

#include <iostream>

ShopSystem::ShopSystem(engine::IRandom& rngIn)
    : rng(rngIn) {
    // Minimal placeholder pool (replace with real odds/config later)
    fallbackPool = {"bulbasaur", "charmander", "squirtle", "pidgey", "rattata"};
}

void ShopSystem::update(engine::ecs::World& /*world*/, float dt) {
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
    // TEMP: keep the shop UI disabled, but still generate a deterministic roll.
    currentCards.clear();
    cardSystem.clearCards();

    if (fallbackPool.empty()) return;

    constexpr int kSlots = 3;
    for (int i = 0; i < kSlots; ++i) {
        const int idx = engine::random::rangeInclusive(rng, 0, static_cast<int>(fallbackPool.size()) - 1);
        CardData cd;
        cd.pokemonName = fallbackPool[static_cast<size_t>(idx)];
        cd.cost = 0;
        cd.type = CardType::Shop;
        currentCards.push_back(cd);
    }
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
