// src/game/GameUpdateGraph.cpp

#include "game/GameUpdateGraph.h"

#include <string>

#include "engine/core/SystemRegistry.h"
#include "engine/ui/BattleFeed.h"

#include "game/GameStateManager.h"
#include "game/GameWorld.h"
#include "game/systems/ShopSystem.h"
#include "game/logging/LogBus.h"

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
    if (inputs_.systems) {
        inputs_.systems->updatePhase(SystemRegistry::Phase::PreUpdate, dt);
        inputs_.systems->updatePhase(SystemRegistry::Phase::Update, dt);
        inputs_.systems->updatePhase(SystemRegistry::Phase::PostUpdate, dt);
    }

    handleRoundPhaseTransitions();

    if (inputs_.stateManager) inputs_.stateManager->update(dt);
    if (inputs_.world) inputs_.world->update(dt);
    if (inputs_.battleFeed) inputs_.battleFeed->update(dt);
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

    lastRoundPhase = current;
}

} // namespace game
