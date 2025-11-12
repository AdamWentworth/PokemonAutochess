// CombatSystem.cpp
#include "CombatSystem.h"
#include "../LuaBindings.h"
#include <iostream>

CombatSystem::CombatSystem(GameWorld* world) : gameWorld(world) {
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
    registerLuaBindings(lua, gameWorld, /*manager*/ nullptr);
    loadScript();
}

void CombatSystem::loadScript() {
    sol::load_result chunk = lua.load_file("scripts/systems/combat.lua");
    if (!chunk.valid()) {
        sol::error e = chunk;
        std::cerr << "[CombatSystem] load error: " << e.what() << "\n";
        ok = false; return;
    }
    sol::protected_function_result r = chunk();
    if (!r.valid()) {
        sol::error e = r;
        std::cerr << "[CombatSystem] exec error: " << e.what() << "\n";
        ok = false; return;
    }
    if (sol::function init = lua["combat_init"]; init.valid()) {
        auto ir = init();
        if (!ir.valid()) {
            sol::error e = ir;
            std::cerr << "[CombatSystem] combat_init error: " << e.what() << "\n";
            ok = false; return;
        }
    }
    ok = true;
}

void CombatSystem::update(float deltaTime) {
    if (!ok) return;
    if (sol::function update = lua["combat_update"]; update.valid()) {
        sol::protected_function_result ur = update(deltaTime);
        if (!ur.valid()) {
            sol::error e = ur;
            std::cerr << "[CombatSystem] combat_update error: " << e.what() << "\n";
            ok = false;
        }
    }
}
