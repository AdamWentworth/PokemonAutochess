// src/game/runtime/session/GameUpdateGraph.cpp

#include "game/runtime/session/GameUpdateGraph.h"

#include <string>
#include <string_view>
#include <chrono>

#include "engine/core/EngineServices.h"
#include "engine/core/ecs/Scheduler.h"
#include "engine/core/ecs/World.h"
#include "game/ecs/RoundState.h"

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

void accumulateFixedSystemMs(EngineFixedPerfBreakdown& stats,
                             std::string_view name,
                             float elapsedMs) {
    if (name == "camera") {
        stats.cameraMs += elapsedMs;
    } else if (name == "unit_interaction") {
        stats.unitInteractionMs += elapsedMs;
    } else if (name == "shop") {
        stats.shopMs += elapsedMs;
    } else if (name == "round") {
        stats.roundMs += elapsedMs;
    } else if (name == "state_manager") {
        stats.stateManagerMs += elapsedMs;
    } else if (name == "movement") {
        stats.movementMs += elapsedMs;
    } else if (name == "combat") {
        stats.combatMs += elapsedMs;
    } else if (name == "world") {
        stats.worldMs += elapsedMs;
    }
}
} // namespace

namespace game {

void GameUpdateGraph::configure(Inputs inputs) {
    inputs_ = inputs;
    hasLastRoundPhase = false;
    if (inputs_.world && inputs_.world->alive(inputs_.roundPhaseEntity)) {
        if (auto* state = inputs_.world->get<game::RoundState>(inputs_.roundPhaseEntity)) {
            lastRoundPhase = state->phase;
            hasLastRoundPhase = true;
            if (inputs_.shopSystem) {
                inputs_.shopSystem->onRoundPhaseChanged(lastRoundPhase, lastRoundPhase);
            }
        }
    }
}

void GameUpdateGraph::tick(float dt) {
    using Clock = std::chrono::high_resolution_clock;
    EngineFixedPerfBreakdown* fixedBreakdown =
        inputs_.engineServices ? &inputs_.engineServices->frameFixedBreakdown : nullptr;

    const auto tickPhase = [&](engine::ecs::Scheduler::Phase phase,
                               float* phaseTotalMs,
                               float* phaseOtherMs) {
        if (!inputs_.scheduler || !inputs_.world) return;
        if (!fixedBreakdown || !phaseTotalMs) {
            inputs_.scheduler->tickPhase(phase, *inputs_.world, dt);
            return;
        }

        const auto phaseStart = Clock::now();
        float observedSystemMs = 0.0f;
        inputs_.scheduler->tickPhaseObserved(
            phase,
            *inputs_.world,
            dt,
            [&](const engine::ecs::ISystem& system, float elapsedMs) {
                observedSystemMs += elapsedMs;
                accumulateFixedSystemMs(*fixedBreakdown, system.debugName(), elapsedMs);
            });
        const float phaseElapsedMs = static_cast<float>(
            std::chrono::duration<double, std::milli>(Clock::now() - phaseStart).count());
        *phaseTotalMs += phaseElapsedMs;
        if (phaseOtherMs) {
            const float otherMs = phaseElapsedMs - observedSystemMs;
            if (otherMs > 0.0f) {
                *phaseOtherMs += otherMs;
            }
        }
    };

    tickPhase(engine::ecs::Scheduler::Phase::PreUpdate,
              fixedBreakdown ? &fixedBreakdown->preUpdateMs : nullptr,
              nullptr);
    tickPhase(engine::ecs::Scheduler::Phase::Update,
              fixedBreakdown ? &fixedBreakdown->updatePhaseMs : nullptr,
              nullptr);

    const auto transitionStart = Clock::now();
    handleRoundPhaseTransitions();
    if (fixedBreakdown) {
        fixedBreakdown->phaseTransitionMs += static_cast<float>(
            std::chrono::duration<double, std::milli>(Clock::now() - transitionStart).count());
    }

    tickPhase(engine::ecs::Scheduler::Phase::PostUpdate,
              fixedBreakdown ? &fixedBreakdown->postUpdateMs : nullptr,
              fixedBreakdown ? &fixedBreakdown->postOtherMs : nullptr);
}

void GameUpdateGraph::handleRoundPhaseTransitions() {
    if (!inputs_.world || !inputs_.shopSystem) return;
    if (!inputs_.world->alive(inputs_.roundPhaseEntity)) return;
    auto* state = inputs_.world->get<game::RoundState>(inputs_.roundPhaseEntity);
    if (!state) return;

    const RoundPhase current = state->phase;
    if (!hasLastRoundPhase) {
        lastRoundPhase = current;
        hasLastRoundPhase = true;
        return;
    }

    if (current == lastRoundPhase) return;

    inputs_.shopSystem->onRoundPhaseChanged(lastRoundPhase, current);

    // Keep phase transitions out of battle feed; state-specific UI owns player-facing messaging.

    if (inputs_.events) {
        const std::string payload = std::string("{\"prev\":\"") + phaseName(lastRoundPhase) +
            "\",\"next\":\"" + phaseName(current) + "\"}";
        inputs_.events->emit("round_phase_changed", payload);
    }
    lastRoundPhase = current;
}

} // namespace game

