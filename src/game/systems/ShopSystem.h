// src/game/systems/ShopSystem.h
#pragma once

#include "engine/core/ecs/ISystem.h"
#include "engine/input/InputEvent.h"
#include "game/systems/RoundPhase.h"
#include "game/systems/CardSystem.h"

#include <vector>
#include <string>

namespace engine { class IRandom; }

// NOTE:
// This is intentionally a minimal, compilation-safe ShopSystem.
// It does not depend on PokemonConfigLoader or any unfinished shop-specific APIs.
class ShopSystem final : public engine::ecs::ISystem {
public:
    explicit ShopSystem(engine::IRandom& rng);
    ~ShopSystem() override = default;

    void update(engine::ecs::World& world, float dt) override;

    // Called by GameApp after translating SDL -> InputEvent
    void handleInput(const InputEvent& event);

    // Called by GameApp when RoundSystem phase changes
    void onRoundPhaseChanged(RoundPhase previous, RoundPhase next);

    // Called by GameApp to draw the shop UI on top of the scene
    void renderUI(int screenWidth, int screenHeight);

private:
    void rollShop();
    void setVisible(bool v);

private:
    bool isVisible = false;

    CardSystem cardSystem;
    std::vector<CardData> currentCards;

    // Temporary pool until you wire in real unit definitions / odds
    std::vector<std::string> fallbackPool;

    engine::IRandom& rng;
};
