// LuaScript.cpp
#include "LuaScript.h"

#include "engine/core/Environment.h"
#include "engine/core/IAssetStore.h"
#include "engine/core/Paths.h"
#include "game/GameWorld.h"
#include "game/GameServices.h"
#include "game/scripting/ScriptAPI.h"
#include "LuaBindings.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

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

struct ScriptSourceCacheEntry {
    bool attemptedRead = false;
    bool readOk = false;
    std::string text;
    std::string error;
};

std::unordered_map<std::string, ScriptSourceCacheEntry>& scriptSourceCache() {
    static std::unordered_map<std::string, ScriptSourceCacheEntry> cache;
    return cache;
}

bool readScriptSourceCached(GameServices& services,
                            const std::string& filePath,
                            std::string& outText,
                            std::string* outError = nullptr) {
    const std::string virt = normalizeVirtualPath(filePath);
    auto& cache = scriptSourceCache();
    auto& entry = cache[virt];
    if (!entry.attemptedRead) {
        entry.attemptedRead = true;
        entry.readOk = services.assets.readText(virt, entry.text, &entry.error);
        if (!entry.readOk) {
            entry.text.clear();
        }
    }

    if (!entry.readOk) {
        outText.clear();
        if (outError) {
            *outError = entry.error.empty()
                ? ("[LuaScript] Asset store read failed: " + virt)
                : ("[LuaScript] Asset store read failed: " + virt + " (" + entry.error + ")");
        }
        return false;
    }

    outText = entry.text;
    if (outError) outError->clear();
    return true;
}

bool loadCachedChunk(sol::state_view lua,
                     GameServices& services,
                     const std::string& filePath,
                     const sol::environment& environment,
                     sol::protected_function& outChunk,
                     std::string* outError = nullptr) {
    std::string text;
    std::string loadError;
    if (!readScriptSourceCached(services, filePath, text, &loadError)) {
        if (outError) *outError = loadError;
        return false;
    }

    sol::load_result chunk = lua.load(text);
    if (!chunk.valid()) {
        sol::error err = chunk;
        if (outError) {
            *outError = std::string("[LuaScript] Failed to load script '") + filePath +
                        "': " + err.what();
        }
        return false;
    }

    outChunk = chunk;
    sol::set_environment(environment, outChunk);
    if (outError) outError->clear();
    return true;
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
    configureRandomSeedFromEnvironment();

    api_ = std::make_unique<ScriptAPI>(gameWorld, stateManager, services_);
    registerBindings();
    resetEnvironment();
    configurePackagePath();
}
LuaScript::~LuaScript() = default;

void LuaScript::configureRandomSeedFromEnvironment() {
    const auto rawSeed = engine::env::get("PAC_RANDOM_SEED");
    if (!rawSeed.has_value()) return;

    try {
        std::size_t parsedCharacters = 0u;
        const unsigned long seed = std::stoul(*rawSeed, &parsedCharacters);
        if (parsedCharacters != rawSeed->size()) return;

        sol::table math = lua["math"];
        sol::protected_function randomSeed = math["randomseed"];
        if (!randomSeed.valid()) return;
        randomSeed(static_cast<lua_Integer>(seed));
    } catch (...) {
    }
}

void LuaScript::registerBindings() {
    if (!api_) {
        api_ = std::make_unique<ScriptAPI>(gameWorld, stateManager, services_);
    }
    registerLuaBindings(lua, *api_);
    lua.set_function(
        "dofile",
        [this](const std::string& filePath,
               sol::this_environment currentEnv,
               sol::this_state ts) -> sol::object {
            sol::state_view L(ts);
            sol::protected_function chunk;
            std::string err;
            if (!loadCachedChunk(L, services_, filePath, currentEnv, chunk, &err)) {
                game::log::error(&services_.log, err);
                throw std::runtime_error(err);
            }
            sol::protected_function_result result = chunk();
            if (!result.valid()) {
                sol::error execError = result;
                const std::string msg =
                    std::string("[LuaScript] Failed to execute script '") + filePath +
                    "': " + execError.what();
                game::log::error(&services_.log, msg);
                throw std::runtime_error(msg);
            }
            if (result.return_count() <= 0) {
                return sol::make_object(L, sol::nil);
            }
            return result.get<sol::object>();
        });
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

    sol::protected_function chunk;
    std::string loadError;
    if (!loadCachedChunk(lua, services_, filePath, env, chunk, &loadError)) {
        game::log::warn(&services_.log, loadError);
        return false;
    }

    sol::protected_function_result r = chunk();
    if (!r.valid()) {
        sol::error execError = r;
        game::log::error(&services_.log, std::string("[LuaScript] Failed to execute script '") + filePath + "': " + execError.what());
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

LuaScript::SourcePrewarmStats LuaScript::prewarmScriptSources(
    GameServices& services,
    const std::vector<std::string>& filePaths) {
    SourcePrewarmStats stats;
    for (const std::string& filePath : filePaths) {
        std::string text;
        std::string err;
        if (readScriptSourceCached(services, filePath, text, &err)) {
            ++stats.warmed;
        } else {
            ++stats.failed;
        }
    }
    return stats;
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
