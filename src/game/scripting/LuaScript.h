// LuaScript.h
#pragma once

#include <memory>
#include <string>
#include <utility>     // std::forward
#include <vector>
#include <sol/sol.hpp>

#include "game/GameServices.h"
#include "game/logging/LoggerUtil.h"

class GameWorld;
class GameStateManager;
class ScriptAPI;

class LuaScript {
public:
    struct SourcePrewarmStats {
        std::size_t warmed = 0u;
        std::size_t failed = 0u;
    };

    explicit LuaScript(GameWorld* world,
                       GameStateManager* manager,
                       GameServices& services);
    ~LuaScript();

    // Loads + executes the script into an isolated environment.
    // Stores file path for later reload().
    bool loadScript(const std::string& filePath);

    static SourcePrewarmStats prewarmScriptSources(
        GameServices& services,
        const std::vector<std::string>& filePaths);

    // Recreates the script environment and re-executes the last loaded script.
    // Returns false if no script was previously loaded or reload fails.
    bool reload();

    void onEnter();
    void onUpdate(float deltaTime);
    void onExit();

    template <typename... Args>
    void call(const std::string& functionName, Args&&... args) {
        // Look up in script environment first (preferred).
        sol::protected_function func = env.valid()
            ? sol::protected_function(env[functionName])
            : sol::protected_function(lua[functionName]);

        // Back-compat fallback: if env doesn't have it, try global.
        if (!func.valid()) {
            func = sol::protected_function(lua[functionName]);
        }
        if (!func.valid()) return;

        sol::protected_function_result r = func(std::forward<Args>(args)...);
        if (!r.valid()) {
            sol::error err = r;
            game::log::error(&services_.log, std::string("[LuaScript] Error in '") + functionName + "': " + err.what());
        }

        flushCommands();
    }

    sol::state& getState();

    // Access the script environment table (where script-defined functions live).
    sol::table getScriptTable();
    void flushCommands();

private:
    sol::state lua;

    // Script runs inside this environment (isolated table that inherits globals).
    sol::environment env;

    GameWorld* gameWorld = nullptr;
    GameStateManager* stateManager = nullptr;
    GameServices& services_;
    std::unique_ptr<ScriptAPI> api_;

    std::string loadedPath;

    void registerBindings();
    void resetEnvironment();
    void configurePackagePath();
};
