// LuaScript.cpp
#include "LuaScript.h"

#include "game/GameWorld.h"
#include "LuaBindings.h"

LuaScript::LuaScript(GameWorld* world, GameStateManager* manager)
    : gameWorld(world), stateManager(manager) {

    lua.open_libraries(
        sol::lib::base,
        sol::lib::math,
        sol::lib::table,
        sol::lib::string,
        sol::lib::package
    );

    registerBindings();
    resetEnvironment();
    configurePackagePath();
}

void LuaScript::registerBindings() {
    registerLuaBindings(lua, gameWorld, stateManager);
}

void LuaScript::resetEnvironment() {
    env = sol::environment(lua, sol::create, lua.globals());
}

void LuaScript::configurePackagePath() {
    // Make script loading resilient to process working-directory differences.
    // Keep existing package.path and prepend your project script roots.
    sol::table package = lua["package"];
    if (!package.valid()) return;

    const std::string existing = package["path"].get_or(std::string());

    // Common Lua module patterns:
    //  - scripts/?.lua
    //  - scripts/?/init.lua
    // Also keep the existing default search path.
    const std::string injected =
        "scripts/?.lua;"
        "scripts/?/init.lua;"
        "scripts/ui/?.lua;"
        "scripts/systems/?.lua;";

    package["path"] = injected + existing;

    // Optional: you can do the same for native modules if you ever add them.
    // const std::string cexisting = package["cpath"].get_or(std::string());
    // package["cpath"] = "scripts/?.dll;" + cexisting;
}

bool LuaScript::loadScript(const std::string& filePath) {
    loadedPath = filePath;

    resetEnvironment();
    configurePackagePath();

    sol::load_result chunk = lua.load_file(filePath);
    if (!chunk.valid()) {
        sol::error err = chunk;
        LogBus::error(std::string("[LuaScript] Failed to load script '") + filePath + "': " + err.what());
        return false;
    }

    sol::protected_function pf = chunk;

    // Older sol2: set environment via free function, not member function.
    sol::set_environment(env, pf);

    sol::protected_function_result r = pf();
    if (!r.valid()) {
        sol::error err = r;
        LogBus::error(std::string("[LuaScript] Failed to execute script '") + filePath + "': " + err.what());
        return false;
    }

    return true;
}

bool LuaScript::reload() {
    if (loadedPath.empty()) {
        LogBus::warn("[LuaScript] reload() called with no previously loaded script");
        return false;
    }
    return loadScript(loadedPath);
}

void LuaScript::onEnter() { call("on_enter"); }
void LuaScript::onUpdate(float dt) { call("on_update", dt); }
void LuaScript::onExit()  { call("on_exit"); }

sol::state& LuaScript::getState() { return lua; }

sol::table LuaScript::getScriptTable() {
    return env;
}
