#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/core/IAssetStore.h"
#include "engine/core/Random.h"
#include "engine/core/TimeSources.h"
#include "game/GameServices.h"
#include "game/config/GameDataDb.h"
#include "game/logging/LogBus.h"
#include "game/scripting/LuaScript.h"
#include "game/scripting/ScriptEventBus.h"

namespace {

struct CountingAssetStore : engine::IAssetStore {
    std::unordered_map<std::string, std::string> textByPath;
    mutable std::unordered_map<std::string, int> readCounts;

    bool readText(const std::string& virtualPath,
                  std::string& outText,
                  std::string* outError = nullptr) const override {
        ++readCounts[virtualPath];
        const auto it = textByPath.find(virtualPath);
        if (it == textByPath.end()) {
            outText.clear();
            if (outError) *outError = "missing";
            return false;
        }
        outText = it->second;
        if (outError) outError->clear();
        return true;
    }

    bool readBytes(const std::string&,
                   std::vector<std::uint8_t>& outBytes,
                   std::string* outError = nullptr) const override {
        outBytes.clear();
        if (outError) *outError = "unsupported";
        return false;
    }

    bool exists(const std::string& virtualPath) const override {
        return textByPath.find(virtualPath) != textByPath.end();
    }

    int readsFor(const std::string& virtualPath) const {
        const auto it = readCounts.find(virtualPath);
        return (it == readCounts.end()) ? 0 : it->second;
    }
};

bool callGetValue(LuaScript& script, int expectedValue, std::string& outFail) {
    sol::table table = script.getScriptTable();
    sol::protected_function getValue = table["get_value"];
    if (!getValue.valid()) {
        outFail = "script environment should expose get_value() after load.";
        return false;
    }
    sol::protected_function_result result = getValue();
    if (!result.valid()) {
        sol::error err = result;
        outFail = std::string("get_value() should execute successfully: ") + err.what();
        return false;
    }
    if (result.get<int>() != expectedValue) {
        outFail = "get_value() should return the value exported by the cached dofile() module.";
        return false;
    }
    return true;
}

} // namespace

bool test_lua_script_source_cache_contract(std::string& outFail) {
    GameConfigData config;
    GameDataDb db;
    LogBus::Logger log;
    ScriptEventBus events;
    CountingAssetStore assets;
    engine::XorShift32 rng(1u);
    engine::ManualTimeSource time;
    GameServices services(config, db, log, events, assets, rng, time);

    const std::string rootPath = "scripts/test/cache_root.lua";
    const std::string sharedPath = "scripts/test/cache_shared.lua";
    assets.textByPath[rootPath] =
        "local shared = dofile(\"scripts/test/cache_shared.lua\")\n"
        "function get_value()\n"
        "    return shared.answer()\n"
        "end\n";
    assets.textByPath[sharedPath] =
        "local M = {}\n"
        "function M.answer()\n"
        "    return 42\n"
        "end\n"
        "return M\n";

    LuaScript first(nullptr, nullptr, services);
    if (!first.loadScript(rootPath)) {
        outFail = "first LuaScript load should succeed for cached-source test.";
        return false;
    }
    if (!callGetValue(first, 42, outFail)) {
        return false;
    }

    LuaScript second(nullptr, nullptr, services);
    if (!second.loadScript(rootPath)) {
        outFail = "second LuaScript load should succeed for cached-source test.";
        return false;
    }
    if (!callGetValue(second, 42, outFail)) {
        return false;
    }

    if (assets.readsFor(rootPath) != 1 || assets.readsFor(sharedPath) != 1) {
        outFail =
            "LuaScript should cache root and dofile() script sources across separate Lua states.";
        return false;
    }

    return true;
}

bool test_lua_script_source_prewarm_contract(std::string& outFail) {
    GameConfigData config;
    GameDataDb db;
    LogBus::Logger log;
    ScriptEventBus events;
    CountingAssetStore assets;
    engine::XorShift32 rng(2u);
    engine::ManualTimeSource time;
    GameServices services(config, db, log, events, assets, rng, time);

    const std::string rootPath = "scripts/test/prewarm_root.lua";
    const std::string sharedPath = "scripts/test/prewarm_shared.lua";
    assets.textByPath[rootPath] =
        "local shared = dofile(\"scripts/test/prewarm_shared.lua\")\n"
        "function get_value()\n"
        "    return shared.answer()\n"
        "end\n";
    assets.textByPath[sharedPath] =
        "local M = {}\n"
        "function M.answer()\n"
        "    return 7\n"
        "end\n"
        "return M\n";

    const auto stats = LuaScript::prewarmScriptSources(services, {rootPath, sharedPath});
    if (stats.warmed != 2u || stats.failed != 0u) {
        outFail = "LuaScript source prewarm should report both known scripts as warmed.";
        return false;
    }
    if (assets.readsFor(rootPath) != 1 || assets.readsFor(sharedPath) != 1) {
        outFail = "LuaScript source prewarm should read each script path exactly once.";
        return false;
    }

    LuaScript script(nullptr, nullptr, services);
    if (!script.loadScript(rootPath)) {
        outFail = "LuaScript load should succeed after script source prewarm.";
        return false;
    }
    if (!callGetValue(script, 7, outFail)) {
        return false;
    }

    if (assets.readsFor(rootPath) != 1 || assets.readsFor(sharedPath) != 1) {
        outFail =
            "LuaScript load should reuse prewarmed root and dofile() script sources without extra asset reads.";
        return false;
    }

    return true;
}
