// RoundSystem.cpp
#include "RoundSystem.h"
#include <iostream>
#include "../../engine/events/EventManager.h"
#include <sol/sol.hpp>  // <- helps IntelliSense since we use sol::function here

static const char* kRoundSystemScript = "scripts/systems/round_system.lua";
static const char* kFnInit   = "rs_init";
static const char* kFnUpdate = "rs_update";
static const char* kFnPhase  = "rs_get_phase";

static std::string phaseName(RoundPhase p) {
    switch (p) {
        case RoundPhase::Planning:   return "Planning";
        case RoundPhase::Battle:     return "Battle";
        case RoundPhase::Resolution: return "Resolution";
    }
    return "Unknown";
}

RoundPhase RoundSystem::toPhaseEnum(const std::string& s) {
    if (s == "Planning")   return RoundPhase::Planning;
    if (s == "Battle")     return RoundPhase::Battle;
    if (s == "Resolution") return RoundPhase::Resolution;
    return RoundPhase::Planning;
}

RoundSystem::RoundSystem()
    : script(/*world*/ nullptr, /*manager*/ nullptr)  // <-- was script(nullptr)
{
    if (!script.loadScript(kRoundSystemScript)) {
        std::cerr << "[RoundSystem] Failed to load " << kRoundSystemScript << "\n";
        currentPhase = RoundPhase::Planning;
        return;
    }

    // Initialize in Lua (if present)
    sol::function fInit = script.getState()[kFnInit];
    if (fInit.valid()) fInit();

    // Read initial phase safely
    sol::function fPhase = script.getState()[kFnPhase];
    if (fPhase.valid()) {
        sol::protected_function_result r = fPhase();
        if (r.valid()) {
            currentPhase = toPhaseEnum(r.get<std::string>());
        } else {
            currentPhase = RoundPhase::Planning;
        }
    } else {
        currentPhase = RoundPhase::Planning;
    }

    std::cout << "[RoundSystem] Starting in " << phaseName(currentPhase) << " Phase\n";
}

void RoundSystem::update(float deltaTime) {
    sol::function fUpdate = script.getState()[kFnUpdate];
    if (fUpdate.valid()) fUpdate(deltaTime);

    sol::function fPhase = script.getState()[kFnPhase];
    if (fPhase.valid()) {
        sol::protected_function_result r = fPhase();
        if (r.valid()) {
            currentPhase = toPhaseEnum(r.get<std::string>());
        }
    }
}

RoundPhase RoundSystem::getCurrentPhase() const {
    return currentPhase;
}
