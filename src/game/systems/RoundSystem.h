// RoundSystem.h

#pragma once

#include "engine/core/ecs/ISystem.h"
#include "engine/core/ecs/Entity.h"
#include "game/GameServices.h"
#include "game/scripting/LuaScript.h"
#include "game/systems/RoundPhase.h"


class RoundSystem final : public engine::ecs::ISystem {
public:
    RoundSystem(GameServices& services, engine::ecs::Entity phaseEntity);

    const char* debugName() const override { return "round"; }
    void update(engine::ecs::World& world, float deltaTime) override;
    RoundPhase getCurrentPhase() const;
    void debugSetPhase(RoundPhase phase, float timerSeconds = -1.0f);

private:
    // The Lua script owns the timing/transition logic.
    LuaScript script;

    // Cached current phase (mirrors Lua state to keep C++ call sites unchanged)
    RoundPhase currentPhase = RoundPhase::Planning;

    // Helper to map string -> enum coming back from Lua
    static RoundPhase toPhaseEnum(const std::string& s);

    engine::ecs::Entity phaseEntity;
};
