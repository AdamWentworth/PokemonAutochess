// RoundSystem.cpp

#include "RoundSystem.h"
#include <iostream>
#include "../../engine/events/EventManager.h"

RoundSystem::RoundSystem() {
    currentPhase = RoundPhase::Planning;
    phaseTimer = planningDuration;
    std::cout << "[RoundSystem] Starting in Planning Phase\n";
}

void RoundSystem::update(float deltaTime) {
    phaseTimer -= deltaTime;
    if (phaseTimer <= 0.0f) {
        advancePhase();
    }
}

RoundPhase RoundSystem::getCurrentPhase() const {
    return currentPhase;
}

void RoundSystem::advancePhase() {
    auto phaseName = [](RoundPhase p) {
        switch (p) {
            case RoundPhase::Planning:    return "Planning";
            case RoundPhase::Battle:      return "Battle";
            case RoundPhase::Resolution:  return "Resolution";
        }
      return "Unknown";
    };

    RoundPhase previous = currentPhase;

    switch (currentPhase) {
        case RoundPhase::Planning:
            currentPhase = RoundPhase::Battle;
            phaseTimer  = battleDuration;
            break;

        case RoundPhase::Battle:
            currentPhase = RoundPhase::Resolution;
            phaseTimer  = resolutionDuration;
            break;

        case RoundPhase::Resolution:
            currentPhase = RoundPhase::Planning;
            phaseTimer  = planningDuration;
            break;
    }

    std::cout << "[RoundSystem] Transition " << phaseName(previous)
              << " → " << phaseName(currentPhase) << "\n";

   // ❸ Emit the change so listeners can react
    RoundPhaseChangedEvent evt(phaseName(previous), phaseName(currentPhase));
    EventManager::getInstance().emit(evt);
}

