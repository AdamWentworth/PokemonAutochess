// tests/TestLuaBindings.cpp
#include <sol/sol.hpp>
#include <string>

#include "game/scripting/LuaBindings.h"

static bool has(sol::state& lua, const char* name) {
    sol::object o = lua[name];
    return o.valid();
}

bool test_lua_bindings_smoke(std::string& outFail) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string, sol::lib::package);

    // Smoke: should not crash even with null world/manager (bindings should guard internally).
    registerLuaBindings(lua, nullptr, nullptr, nullptr);

    const char* required[] = {
        "emit",
        "spawnPokemon",
        "spawn_on_grid",
        "push_state",
        "pop_state",
        "world_list_units",
        "world_get_unit_snapshot",
        "world_apply_move",
        "world_commit_move",
        "world_apply_damage",
        "grid_to_world",
        "world_to_grid",
        "unit_fast_move",
        "unit_charged_move",
        "move_get",
    };

    for (auto* fn : required) {
        if (!has(lua, fn)) {
            outFail = std::string("Missing Lua binding: ") + fn;
            return false;
        }
    }

    return true;
}
