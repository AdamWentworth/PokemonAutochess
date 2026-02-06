// tests/TestLuaBindings.cpp
#include <sol/sol.hpp>
#include <iostream>

#include "game/scripting/LuaBindings.h"

static bool has(sol::state& lua, const char* name) {
    sol::object o = lua[name];
    return o.valid();
}

int main() {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string, sol::lib::package);

    // Smoke: should not crash even with null world/manager (bindings guard internally).
    registerLuaBindings(lua, nullptr, nullptr);

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

    bool ok = true;
    for (auto* fn : required) {
        if (!has(lua, fn)) {
            std::cerr << "[PAC_Tests] Missing Lua binding: " << fn << "\n";
            ok = false;
        }
    }

    if (!ok) return 1;

    std::cout << "[PAC_Tests] Lua binding smoke test passed.\n";
    return 0;
}
