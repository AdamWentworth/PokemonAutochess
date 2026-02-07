// src/game/systems/ShopSystem.h
#pragma once

#include "engine/core/Updatable.h"
#include "engine/input/InputEvent.h"
#include "game/systems/RoundSystem.h"
#include "game/systems/CardSystem.h"

#include <vector>
#include <string>
#include <memory>

namespace engine { class IRandom; }

// NOTE:
// This is intentionally a minimal, compilation-safe ShopSystem.
// It does not depend on PokemonConfigLoader or any unfinished shop-specific APIs.
class ShopSystem : public Updatable {
public:
    explicit ShopSystem(engine::IRandom* rng = nullptr);
    ~ShopSystem() override = default;

    void update(float dt) override;

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

    engine::IRandom* rng = nullptr;
    std::unique_ptr<engine::IRandom> fallbackRng;
};
