// MovementSystem.cpp
#include "MovementSystem.h"
#include "game/scripting/LuaBindings.h"
#include "game/scripting/ScriptAPI.h"
#include "game/GameServices.h"
#include "engine/core/IAssetStore.h"
#include "engine/core/Paths.h"
#include <iostream>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

class GridOccupancy;

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

bool loadLuaFromStore(sol::state& lua, const std::string& path, engine::IAssetStore& assets, std::string& outErr) {
    std::string text;
    std::string err;
    const std::string virt = normalizeVirtualPath(path);
    if (!assets.readText(virt, text, &err)) {
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

static const char* kMovementLua = "scripts/systems/movement.lua";

MovementSystem::MovementSystem(GameWorld* world, GameServices& svc)
    : gameWorld(world), services(svc)
{
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
    api = std::make_unique<ScriptAPI>(gameWorld, /*GameStateManager*/ nullptr, services);
    registerLuaBindings(lua, *api);
    exposeConstants();
    loadScript();
}

void MovementSystem::exposeConstants() {
    // Make grid constants available to Lua scripts
    lua["GRID_COLS"]  = GRID_COLS;
    lua["GRID_ROWS"]  = GRID_ROWS;
    lua["CELL_SIZE"]  = CELL_SIZE;
}

void MovementSystem::loadScript() {
    // Load and run the Lua movement system
    std::string err;
    if (!loadLuaFromStore(lua, kMovementLua, services.assets, err)) {
        std::cerr << "[MovementSystem] Failed to load movement.lua: " << err << "\n";
        ok = false;
        return;
    }

    // Optional init function in Lua
    sol::function init = lua["movement_init"];
    if (init.valid()) {
        sol::protected_function_result ir = init();
        if (!ir.valid()) {
            sol::error e = ir;
            std::cerr << "[MovementSystem] movement_init() error: " << e.what() << "\n";
            ok = false;
            return;
        }
    }

    ok = true;
}

void MovementSystem::update(float deltaTime) {
    if (!ok) return;

    // 1) Let Lua compute winners and start committed one-cell moves.
    if (sol::function updateFn = lua["movement_update"]; updateFn.valid()) {
        sol::protected_function_result ur = updateFn(deltaTime);
        if (!ur.valid()) {
            sol::error e = ur;
            std::cerr << "[MovementSystem] movement_update(dt) error: " << e.what() << "\n";
            ok = false; // prevent spamming on repeated errors
            return;
        }
    }
    if (api) api->flush();

    // 2) Advance interpolation for units that have an active commit
    //    Distance per second is movementSpeed * CELL_SIZE (cells/sec * worldUnitsPerCell).
    auto& units = gameWorld->getPokemons();

    for (auto& u : units) {
        if (!u.alive) continue;
        if (!u.isMoving) continue;

        const glm::vec3 toVec = u.moveTo - u.position;
        const float dist = glm::length(toVec);
        if (dist <= 1e-4f) {
            // Arrived (or already there)
            u.position = u.moveTo;
            u.isMoving = false;
            u.moveT = 1.0f;
            u.committedDest = {-1,-1};
            continue;
        }

        const glm::vec3 dir = toVec / dist;
        const float step = u.movementSpeed * CELL_SIZE * deltaTime; // world units per frame
        if (step >= dist) {
            // Finish the move this frame
            u.position = u.moveTo;
            u.isMoving = false;
            u.moveT = 1.0f;
            u.committedDest = {-1,-1};
        } else {
            // Advance toward destination
            u.position += dir * step;
            // Rough progress: step / one-cell distance (CELL_SIZE)
            u.moveT = std::min(1.0f, u.moveT + (step / (CELL_SIZE + 1e-4f)));
        }
    }
}
