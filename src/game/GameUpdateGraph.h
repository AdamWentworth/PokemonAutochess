// src/game/GameUpdateGraph.h
#pragma once

#include "game/systems/RoundSystem.h"

class SystemRegistry;
class GameStateManager;
class GameWorld;
class BattleFeed;
class ShopSystem;
namespace LogBus { class Logger; }

namespace game {

// Single source of truth for runtime update ordering.
// Keeps the "what updates when" policy explicit and testable.
class GameUpdateGraph {
public:
    struct Inputs {
        SystemRegistry* systems = nullptr;
        RoundSystem* roundSystem = nullptr;
        ShopSystem* shopSystem = nullptr;
        GameStateManager* stateManager = nullptr;
        GameWorld* world = nullptr;
        BattleFeed* battleFeed = nullptr;
        LogBus::Logger* log = nullptr;
    };

    void configure(Inputs inputs);
    void tick(float dt);

private:
    void handleRoundPhaseTransitions();

    Inputs inputs_{};
    RoundPhase lastRoundPhase = RoundPhase::Planning;
    bool hasLastRoundPhase = false;
};

} // namespace game
