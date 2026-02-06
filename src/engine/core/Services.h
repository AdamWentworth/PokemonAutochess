// src/engine/core/Services.h

#pragma once
#include "engine/core/ILogger.h"
#include "engine/core/IEventBus.h"

namespace engine {

// Central dependency bundle used for explicit, testable wiring.
// Extend as you add time/random/assets/etc.
struct CoreServices {
    ILogger*  log = nullptr;     // required in practice; null allowed for ultra-minimal tests
    IEventBus* events = nullptr; // required for event-driven code; null allowed for minimal tests
};

} // namespace engine
