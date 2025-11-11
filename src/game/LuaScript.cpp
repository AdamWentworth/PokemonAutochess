// LuaScript.cpp
#include "LuaScript.h"
#include "GameWorld.h"
#include "LuaBindings.h"
#include <iostream>  // <-- add this

LuaScript::LuaScript(GameWorld* world, GameStateManager* manager)
    : gameWorld(world), stateManager(manager) {
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
    registerBindings();
}

bool LuaScript::loadScript(const std::string& filePath) {
    sol::load_result chunk = lua.load_file(filePath);
    if (!chunk.valid()) {
        sol::error err = chunk;
        std::cerr << "[LuaScript] Failed to load script '" << filePath << "': " << err.what() << "\n";
        return false;
    }

    sol::protected_function_result r = chunk();
    if (!r.valid()) {
        sol::error err = r;
        std::cerr << "[LuaScript] Failed to execute script '" << filePath << "': " << err.what() << "\n";
        return false;
    }
    return true;
}

void LuaScript::registerBindings() {
    registerLuaBindings(lua, gameWorld, stateManager);
}

void LuaScript::onEnter()  { call("on_enter"); }
void LuaScript::onUpdate(float dt) { call("on_update", dt); }
void LuaScript::onExit()   { call("on_exit"); }

sol::state& LuaScript::getState() { return lua; }
