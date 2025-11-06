// RoundSystem.h

#pragma once

#include "../../engine/core/IUpdatable.h"
#include "../LuaScript.h"
#include "../../engine/events/RoundEvents.h"

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
    // The Lua script owns the timing/transition logic.
    LuaScript script;

    // Cached current phase (mirrors Lua state to keep C++ call sites unchanged)
    RoundPhase currentPhase = RoundPhase::Planning;

    // Helper to map string -> enum coming back from Lua
    static RoundPhase toPhaseEnum(const std::string& s);
};
