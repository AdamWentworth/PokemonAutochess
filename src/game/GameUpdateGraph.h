// src/game/GameUpdateGraph.h
#pragma once

#include "game/systems/RoundPhase.h"
#include "engine/core/ecs/Entity.h"

class ShopSystem;
namespace LogBus { class Logger; }
class ScriptEventBus;
namespace engine::ecs { class Scheduler; class World; }

namespace game {

// Single source of truth for runtime update ordering.
// Keeps the "what updates when" policy explicit and testable.
class GameUpdateGraph {
public:
    struct Inputs {
        engine::ecs::Scheduler* scheduler = nullptr;
        engine::ecs::World* world = nullptr;
        engine::ecs::Entity roundPhaseEntity{};
        ShopSystem* shopSystem = nullptr;
        LogBus::Logger* log = nullptr;
        ScriptEventBus* events = nullptr;
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
