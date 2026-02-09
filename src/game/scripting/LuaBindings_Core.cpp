// src/game/scripting/LuaBindings.cpp
#include <glm/glm.hpp>
#include "engine/render/Model.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

#include "LuaBindings.h"

#include "game/GameWorld.h"
#include "game/PokemonInstance.h"
#include "game/GameStateManager.h"
#include "game/GameConfig.h"
#include "game/scripting/ScriptAPI.h"
#include "game/scripting/ScriptEventBus.h"

#include "game/animation/FlightLocomotion.h"
#include "game/animation/AttackAnimDebug.h"

#include "game/config/PokemonConfigLoader.h"
#include "game/config/MovesConfigLoader.h"
#include "game/config/AttackAnimConfigLoader.h"
#include "game/config/AnimSetLoader.h"

#include "game/state/ScriptedState.h"

#include "game/logging/LoggerUtil.h"
#include "game/logging/DebugTrace.h"

#include "LuaBindings_Internal.h"

void registerLuaBindings_Core(sol::state& lua, ScriptAPI& api) {
    LogBus::Logger* logger = &api.logger();
    // Basic enums
    lua.new_enum("PokemonSide",
        "Player", PokemonSide::Player,
        "Enemy",  PokemonSide::Enemy
    );

    // ---- Logging: Lua -> BattleFeed ----
    lua.set_function("emit", [&api](const std::string& tag_or_msg, sol::optional<std::string> payload) {
        api.emit(tag_or_msg, payload ? std::optional<std::string>(*payload) : std::nullopt);
    });

    // ---- Engine-safe spawners ----
    lua.set_function("spawnPokemon", [&api](std::string name, float x, float y, float z) {
        api.spawnPokemon(name, x, y, z);
    });
    lua.set_function("spawn_on_bench", [&api](std::string name) {
        api.addToBench(name);
    });
    lua.set_function("spawn_on_grid",
    [&api](std::string name, int col, int row, std::string side, sol::optional<int> level) {
        int lvl = level.value_or(-1);
        api.spawnOnGrid(name, col, row, sideFromString(side), lvl);
    });

    // ---- Round events ----
    // Deprecated: legacy shim kept so existing scripts don't crash.
    // Round phase changes are handled directly in C++ (GameApp / RoundSystem).
    lua.set_function("emit_round_phase_changed",
        [logger](const std::string& prev, const std::string& next) {
            (void)prev; (void)next;
            game::log::warn(logger, "emit_round_phase_changed() is deprecated and currently a no-op");
        }
    );

    // ---- Script event stream ----
    lua.set_function("events_drain", [&api, &lua]() {
        sol::state_view L(lua);
        sol::table arr = L.create_table();
        auto events = api.drainEvents();
        int i = 1;
        for (const auto& e : events) {
            sol::table t = L.create_table();
            t["type"] = e.type;
            if (e.hasPayload) t["payload"] = e.payload;
            arr[i++] = t;
        }
        return arr;
    });

    // ---- State mgmt ----
    lua.set_function("push_state", [&api](const std::string& scriptPath) {
        api.pushState(scriptPath);
    });
    lua.set_function("push_combat_state", [&api](const std::string& scriptPath) {
        api.pushCombatState(scriptPath);
    });
    lua.set_function("pop_state", [&api]() { api.popState(); });

    // =================================================================
    // World/Unit inspection & mutation for Lua systems
    // =================================================================
    
}
