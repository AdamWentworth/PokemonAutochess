// src/game/GameUpdateGraph.cpp

#include "game/GameUpdateGraph.h"

#include <string>

#include "engine/core/ecs/Scheduler.h"
#include "engine/core/ecs/World.h"

#include "game/systems/ShopSystem.h"
#include "game/logging/LogBus.h"
#include "game/scripting/ScriptEventBus.h"

namespace {
const char* phaseName(RoundPhase p) {
    switch (p) {
        case RoundPhase::Planning:   return "Planning";
        case RoundPhase::Battle:     return "Battle";
        case RoundPhase::Resolution: return "Resolution";
        default:                     return "Unknown";
    }
}
} // namespace

namespace game {

void GameUpdateGraph::configure(Inputs inputs) {
    inputs_ = inputs;
    hasLastRoundPhase = false;
    if (inputs_.roundSystem) {
        lastRoundPhase = inputs_.roundSystem->getCurrentPhase();
        hasLastRoundPhase = true;
        if (inputs_.shopSystem) {
            inputs_.shopSystem->onRoundPhaseChanged(lastRoundPhase, lastRoundPhase);
        }
    }
}

void GameUpdateGraph::tick(float dt) {
    if (inputs_.scheduler && inputs_.world) {
        inputs_.scheduler->tickPhase(engine::ecs::Scheduler::Phase::PreUpdate, *inputs_.world, dt);
        inputs_.scheduler->tickPhase(engine::ecs::Scheduler::Phase::Update, *inputs_.world, dt);
    }

    handleRoundPhaseTransitions();

    if (inputs_.scheduler && inputs_.world) {
        inputs_.scheduler->tickPhase(engine::ecs::Scheduler::Phase::PostUpdate, *inputs_.world, dt);
    }
}

void GameUpdateGraph::handleRoundPhaseTransitions() {
    if (!inputs_.roundSystem || !inputs_.shopSystem) return;

    const RoundPhase current = inputs_.roundSystem->getCurrentPhase();
    if (!hasLastRoundPhase) {
        lastRoundPhase = current;
        hasLastRoundPhase = true;
        return;
    }

    if (current == lastRoundPhase) return;

    inputs_.shopSystem->onRoundPhaseChanged(lastRoundPhase, current);

    if (inputs_.log) {
        inputs_.log->colored(
            std::string("Phase: ") + phaseName(lastRoundPhase) +
                " \xE2\x86\x92 " + phaseName(current),
            {0.75f, 0.9f, 1.0f},
            3.0f
        );
    }

    if (inputs_.events) {
        const std::string payload = std::string("{\"prev\":\"") + phaseName(lastRoundPhase) +
            "\",\"next\":\"" + phaseName(current) + "\"}";
        inputs_.events->emit("round_phase_changed", payload);
    }
    lastRoundPhase = current;
}

} // namespace game
