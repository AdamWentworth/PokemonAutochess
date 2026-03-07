// src/game/systems/ShopSystem.h
#pragma once

#include "engine/core/ecs/ISystem.h"
#include "game/systems/RoundPhase.h"

#include <vector>
#include <string>

namespace engine { class IRandom; }

// Lightweight phase-driven shop offer service.
// ShopSystem tracks deterministic shop offers during Planning phase;
// rendering/input ownership lives with state/UI layers.
class ShopSystem final : public engine::ecs::ISystem {
public:
    struct ShopOffer {
        std::string pokemonName;
        int level = 1;
        int cost = 0;
    };

    explicit ShopSystem(engine::IRandom& rng);
    ~ShopSystem() override = default;

    const char* debugName() const override { return "shop"; }
    void update(engine::ecs::World& world, float dt) override;

    // Called when RoundSystem phase changes.
    void onRoundPhaseChanged(RoundPhase previous, RoundPhase next);

    bool isVisible() const { return isVisible_; }
    const std::vector<ShopOffer>& offers() const { return offers_; }
    bool reroll();

private:
    void rollShop();
    void setVisible(bool visible);

private:
    bool isVisible_ = false;
    std::vector<ShopOffer> offers_;

    // Fallback pool until weighted shop tables are wired from config.
    std::vector<std::string> fallbackPool;
    int slotsPerRoll = 3;

    engine::IRandom& rng;
};
