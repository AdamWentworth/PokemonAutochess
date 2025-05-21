// RoundSystem.h

#pragma once

#include "../../engine/core/IUpdatable.h"
#include "../../engine/events/EventManager.h"
#include "../../engine/events/RoundEvents.h"
#include <string>

enum class RoundPhase {
    Planning,
    Battle,
    Resolution
};

class RoundSystem : public IUpdatable {
public:
    RoundSystem();

    void update(float deltaTime) override;
    RoundPhase getCurrentPhase() const;

private:
    void advancePhase();

    RoundPhase currentPhase = RoundPhase::Planning;
    float phaseTimer = 0.0f;
    float planningDuration = 30.0f;   // seconds
    float battleDuration = 10.0f;
    float resolutionDuration = 5.0f;
};
