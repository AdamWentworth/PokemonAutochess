// src/game/systems/RoundSystem.cpp
#include "RoundSystem.h"
#include "game/GameServices.h"
#include "game/PhaseState.h"
#include "engine/core/ecs/World.h"

#include <iostream>
#include <sol/sol.hpp>

static const char* kRoundSystemScript = "scripts/systems/round_system.lua";
static const char* kFnInit   = "rs_init";
static const char* kFnUpdate = "rs_update";
static const char* kFnPhase  = "rs_get_phase";
static const char* kFnDebugSetState = "rs_debug_set_state";

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

    // Initialize in Lua (if present) from script environment (preferred).
    sol::table S = script.getScriptTable();
    sol::function fInit;
    if (S.valid()) fInit = S.get<sol::function>(kFnInit);
    if (!fInit.valid()) fInit = script.getState().get<sol::function>(kFnInit);
    if (fInit.valid()) fInit();
    script.flushCommands();

    // Read initial phase safely
    sol::function fPhase;
    if (S.valid()) fPhase = S.get<sol::function>(kFnPhase);
    if (!fPhase.valid()) fPhase = script.getState().get<sol::function>(kFnPhase);
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
    sol::table S = script.getScriptTable();
    sol::function fUpdate;
    if (S.valid()) fUpdate = S.get<sol::function>(kFnUpdate);
    if (!fUpdate.valid()) fUpdate = script.getState().get<sol::function>(kFnUpdate);
    if (fUpdate.valid()) fUpdate(deltaTime);
    script.flushCommands();

    sol::function fPhase;
    if (S.valid()) fPhase = S.get<sol::function>(kFnPhase);
    if (!fPhase.valid()) fPhase = script.getState().get<sol::function>(kFnPhase);
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

void RoundSystem::debugSetPhase(RoundPhase phase, float timerSeconds) {
    currentPhase = phase;

    sol::table S = script.getScriptTable();
    sol::function fDebugSet;
    if (S.valid()) fDebugSet = S.get<sol::function>(kFnDebugSetState);
    if (!fDebugSet.valid()) fDebugSet = script.getState().get<sol::function>(kFnDebugSetState);

    if (!fDebugSet.valid()) {
        return;
    }

    const std::string phaseToken = phaseName(phase);
    sol::protected_function_result r = fDebugSet(phaseToken, timerSeconds);
    if (!r.valid()) {
        // Keep currentPhase cache even if debug helper was not accepted by Lua.
        return;
    }
}
