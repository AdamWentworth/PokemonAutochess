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

void registerLuaBindings_Core(sol::state& lua, GameWorld* world, GameStateManager* manager, LogBus::Logger* logger) {
    // Basic enums
    lua.new_enum("PokemonSide",
        "Player", PokemonSide::Player,
        "Enemy",  PokemonSide::Enemy
    );

    // ---- Logging: Lua -> BattleFeed ----
    lua.set_function("emit", [logger](const std::string& tag_or_msg, sol::optional<std::string> payload) {
        if (payload.has_value() && !payload->empty()) {
            // Structured (verbose) log -> terminal only
            const std::string& tag = tag_or_msg;
            const bool hasBrackets = !tag.empty() && tag.front()=='[' && tag.back()==']';
            const std::string header = hasBrackets ? tag : ("[" + tag + "]");
            game::log::infoTerminalOnly(logger, header + " " + *payload);
        } else {
            // Human-readable line -> show in on-screen feed (and mirror to terminal)
            game::log::info(logger, tag_or_msg);
        }
    });

    // ---- Engine-safe spawners ----
    lua.set_function("spawnPokemon", [world](std::string name, float x, float y, float z) {
        if (world) world->spawnPokemon(name, {x, y, z});
    });
    lua.set_function("spawn_on_grid",
    [world](std::string name, int col, int row, std::string side, sol::optional<int> level) {
        int lvl = level.value_or(-1);
        if (world) world->spawnPokemonAtGrid(name, col, row, sideFromString(side), lvl);
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

    // ---- State mgmt ----
    lua.set_function("push_state", [manager, world](const std::string& scriptPath) {
        if (!manager) return;
        manager->pushState(std::make_unique<ScriptedState>(manager, world, scriptPath));
    });
    lua.set_function("pop_state", [manager]() { if (manager) manager->popState(); });

    // =================================================================
    // World/Unit inspection & mutation for Lua systems
    // =================================================================
    
}
