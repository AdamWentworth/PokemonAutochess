// ShopSystem.cpp
#include "game/systems/ShopSystem.h"

#include "engine/core/Random.h"

ShopSystem::ShopSystem(engine::IRandom& rngIn)
    : rng(rngIn) {
    // Deterministic fallback pool; replace with weighted config tables later.
    fallbackPool = {"bulbasaur", "charmander", "squirtle", "pidgey", "rattata"};
}

void ShopSystem::update(engine::ecs::World& /*world*/, float dt) {
    (void)dt;
    // Event/phase-driven system; no per-frame logic yet.
}

void ShopSystem::setVisible(bool visible) {
    if (visible == isVisible_) return;
    isVisible_ = visible;

    // Hide -> clear stale offers.
    if (!isVisible_) {
        offers_.clear();
    }
}

void ShopSystem::onRoundPhaseChanged(RoundPhase previous, RoundPhase next) {
    const bool wasVisible = isVisible_;
    setVisible(next == RoundPhase::Planning);
    if (isVisible_ && (!wasVisible || previous != RoundPhase::Planning)) {
        rollShop();
    }
}

void ShopSystem::rollShop() {
    offers_.clear();

    if (fallbackPool.empty()) return;
    if (slotsPerRoll <= 0) return;

    for (int i = 0; i < slotsPerRoll; ++i) {
        const int idx = engine::random::rangeInclusive(rng, 0, static_cast<int>(fallbackPool.size()) - 1);
        ShopOffer offer;
        offer.pokemonName = fallbackPool[static_cast<size_t>(idx)];
        offer.level = 1;
        offer.cost = 0;
        offers_.push_back(std::move(offer));
    }
}

bool ShopSystem::reroll() {
    if (!isVisible_) return false;
    if (fallbackPool.empty()) return false;
    rollShop();
    return true;
}
