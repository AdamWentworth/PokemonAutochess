// LuaScript.cpp

#include "LuaScript.h"
#include "GameWorld.h"
#include "LuaBindings.h"
#include <iostream>

LuaScript::LuaScript(GameWorld* world)
    : gameWorld(world)
{
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
    registerBindings();
}

bool LuaScript::loadScript(const std::string& filePath) {
    sol::load_result script = lua.load_file(filePath);
    if (!script.valid()) {
        sol::error err = script;
        std::cerr << "[LuaScript] Failed to load script: " << err.what() << "\n";
        return false;
    }

    sol::protected_function_result result = script();
    if (!result.valid()) {
        sol::error err = result;
        std::cerr << "[LuaScript] Failed to execute script: " << err.what() << "\n";
        return false;
    }

    return true;
}

void LuaScript::registerBindings() {
    registerLuaBindings(lua, gameWorld);
}

void LuaScript::onEnter() {
    call("onEnter");
}

void LuaScript::onUpdate(float deltaTime) {
    call("onUpdate", deltaTime);
}

void LuaScript::onExit() {
    call("onExit");
}

sol::state& LuaScript::getState() {
    return lua;
}
