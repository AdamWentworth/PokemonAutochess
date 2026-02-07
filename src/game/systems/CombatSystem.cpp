// CombatSystem.cpp
#include "CombatSystem.h"
#include "game/GameWorld.h"
#include "game/scripting/LuaBindings.h"
#include "game/scripting/ScriptAPI.h"
#include "engine/core/IAssetStore.h"
#include "engine/core/Paths.h"
#include <iostream>
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

bool loadLuaFromStore(sol::state& lua, const std::string& path, engine::IAssetStore* assets, std::string& outErr) {
    if (!assets) return false;
    std::string text;
    std::string err;
    const std::string virt = normalizeVirtualPath(path);
    if (!assets->readText(virt, text, &err)) {
        outErr = err.empty() ? ("Failed to read " + virt) : err;
        return false;
    }
    sol::load_result chunk = lua.load(text);
    if (!chunk.valid()) {
        sol::error e = chunk;
        outErr = e.what();
        return false;
    }
    sol::protected_function_result r = chunk();
    if (!r.valid()) {
        sol::error e = r;
        outErr = e.what();
        return false;
    }
    return true;
}
} // namespace

static const char* kCombatLua = "scripts/systems/combat.lua";

CombatSystem::CombatSystem(GameWorld* world, ScriptEventBus* events, engine::IAssetStore* assets)
    : gameWorld(world), assetStore(assets) {
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
    LogBus::Logger* logger = gameWorld ? gameWorld->getLogger() : nullptr;
    api = std::make_unique<ScriptAPI>(gameWorld, /*manager*/ nullptr, logger, events);
    registerLuaBindings(lua, *api);
    loadScript();
}

void CombatSystem::loadScript() {
    std::string err;
    if (assetStore && loadLuaFromStore(lua, kCombatLua, assetStore, err)) {
        ok = true;
    } else {
        sol::load_result chunk = lua.load_file(kCombatLua);
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
    if (api) api->flush();
}
