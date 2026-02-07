// tests/TestLuaBindings.cpp
#include <sol/sol.hpp>
#include <string>

#include "engine/core/IAssetStore.h"
#include "engine/core/Random.h"
#include "engine/core/TimeSources.h"
#include "game/GameServices.h"
#include "game/config/GameDataDb.h"
#include "game/logging/LogBus.h"
#include "game/scripting/LuaBindings.h"
#include "game/scripting/ScriptAPI.h"
#include "game/scripting/ScriptEventBus.h"

namespace {
struct NullAssetStore : engine::IAssetStore {
    bool readText(const std::string&, std::string& outText, std::string* outError = nullptr) const override {
        outText.clear();
        if (outError) *outError = "NullAssetStore";
        return false;
    }
    bool readBytes(const std::string&, std::vector<std::uint8_t>& outBytes, std::string* outError = nullptr) const override {
        outBytes.clear();
        if (outError) *outError = "NullAssetStore";
        return false;
    }
    bool exists(const std::string&) const override { return false; }
};
} // namespace

static bool has(sol::state& lua, const char* name) {
    sol::object o = lua[name];
    return o.valid();
}

bool test_lua_bindings_smoke(std::string& outFail) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string, sol::lib::package);

    GameConfigData config;
    GameDataDb db;
    LogBus::Logger log;
    ScriptEventBus events;
    NullAssetStore assets;
    engine::XorShift32 rng(1u);
    engine::ManualTimeSource time;
    GameServices services(config, db, log, events, assets, rng, time);

    // Smoke: should not crash even with null world/manager (bindings should guard internally).
    ScriptAPI api(nullptr, nullptr, services);
    registerLuaBindings(lua, api);

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
