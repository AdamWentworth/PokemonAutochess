// LuaScript.cpp
#include "LuaScript.h"

#include "engine/core/IAssetStore.h"
#include "engine/core/Paths.h"
#include "game/GameWorld.h"
#include "game/GameServices.h"
#include "game/scripting/ScriptAPI.h"
#include "LuaBindings.h"

#include <algorithm>

namespace {
std::string normalizeVirtualPath(std::string path) {
    std::string root = engine::paths::dataRoot();
    std::replace(root.begin(), root.end(), '\\', '/');
    std::replace(path.begin(), path.end(), '\\', '/');
    if (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    if (!root.empty() && path.rfind(root + "/", 0) == 0) {
        path = path.substr(root.size() + 1);
    }
    while (!path.empty() && (path.front() == '/' || path.front() == '\\')) {
        path.erase(path.begin());
    }
    return path;
}
} // namespace

LuaScript::LuaScript(GameWorld* world,
                     GameStateManager* manager,
                     GameServices& services)
    : gameWorld(world), stateManager(manager), services_(services) {

    lua.open_libraries(
        sol::lib::base,
        sol::lib::math,
        sol::lib::table,
        sol::lib::string,
        sol::lib::package
    );

    api_ = std::make_unique<ScriptAPI>(gameWorld, stateManager, services_);
    registerBindings();
    resetEnvironment();
    configurePackagePath();
}
LuaScript::~LuaScript() = default;

void LuaScript::registerBindings() {
    if (!api_) {
        api_ = std::make_unique<ScriptAPI>(gameWorld, stateManager, services_);
    }
    registerLuaBindings(lua, *api_);
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

    std::string text;
    std::string err;
    const std::string virt = normalizeVirtualPath(filePath);
    if (!services_.assets.readText(virt, text, &err)) {
        const std::string msg = err.empty()
            ? ("[LuaScript] Asset store read failed: " + virt)
            : ("[LuaScript] Asset store read failed: " + virt + " (" + err + ")");
        game::log::warn(&services_.log, msg);
        return false;
    }
    sol::load_result chunk = lua.load(text);
    if (!chunk.valid()) {
        sol::error err = chunk;
        game::log::error(&services_.log, std::string("[LuaScript] Failed to load script '") + filePath + "': " + err.what());
        return false;
    }

    sol::protected_function pf = chunk;

    // Older sol2: set environment via free function, not member function.
    sol::set_environment(env, pf);

    sol::protected_function_result r = pf();
    if (!r.valid()) {
        sol::error err = r;
        game::log::error(&services_.log, std::string("[LuaScript] Failed to execute script '") + filePath + "': " + err.what());
        return false;
    }

    return true;
}

bool LuaScript::reload() {
    if (loadedPath.empty()) {
        game::log::warn(&services_.log, "[LuaScript] reload() called with no previously loaded script");
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

void LuaScript::flushCommands() {
    if (api_) api_->flush();
}
