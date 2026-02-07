// src/game/systems/RoundSystem.cpp
#include "RoundSystem.h"
#include "game/GameServices.h"
#include "game/ecs/RoundState.h"
#include "engine/core/ecs/World.h"

#include <iostream>
#include <sol/sol.hpp>

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

RoundSystem::RoundSystem(GameServices& services, engine::ecs::Entity phaseEntity_)
    : script(/*world*/ nullptr, /*manager*/ nullptr, services)
    , phaseEntity(phaseEntity_)
{
    if (!script.loadScript(kRoundSystemScript)) {
        std::cerr << "[RoundSystem] Failed to load " << kRoundSystemScript << "\n";
        currentPhase = RoundPhase::Planning;
        return;
    }

    // Initialize in Lua (if present)
    sol::function fInit = script.getState()[kFnInit];
    if (fInit.valid()) fInit();
    script.flushCommands();

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

void RoundSystem::update(engine::ecs::World& world, float deltaTime) {
    sol::function fUpdate = script.getState()[kFnUpdate];
    if (fUpdate.valid()) fUpdate(deltaTime);
    script.flushCommands();

    sol::function fPhase = script.getState()[kFnPhase];
    if (fPhase.valid()) {
        sol::protected_function_result r = fPhase();
        if (r.valid()) {
            currentPhase = toPhaseEnum(r.get<std::string>());
        }
    }

    if (world.alive(phaseEntity)) {
        if (auto* state = world.get<game::RoundState>(phaseEntity)) {
            state->phase = currentPhase;
        } else {
            world.add<game::RoundState>(phaseEntity, game::RoundState{ currentPhase });
        }
    }
}

RoundPhase RoundSystem::getCurrentPhase() const {
    return currentPhase;
}
