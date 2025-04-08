// RoundSystem.cpp

#include "RoundSystem.h"
#include <iostream>

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
    switch (currentPhase) {
        case RoundPhase::Planning:
            currentPhase = RoundPhase::Battle;
            phaseTimer = battleDuration;
            std::cout << "[RoundSystem] Transition to Battle Phase\n";
            break;

        case RoundPhase::Battle:
            currentPhase = RoundPhase::Resolution;
            phaseTimer = resolutionDuration;
            std::cout << "[RoundSystem] Transition to Resolution Phase\n";
            break;

        case RoundPhase::Resolution:
            currentPhase = RoundPhase::Planning;
            phaseTimer = planningDuration;
            std::cout << "[RoundSystem] New Round: Back to Planning Phase\n";
            break;
    }
}
