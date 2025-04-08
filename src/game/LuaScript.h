// LuaScript.h

#pragma once

#include <string>
#include <sol/sol.hpp>

class GameWorld;

class LuaScript {
public:
    LuaScript(GameWorld* world);
    ~LuaScript() = default;

    // Loads and executes a Lua script file
    bool loadScript(const std::string& filePath);

    // Hooked lifecycle calls
    void onEnter();
    void onUpdate(float deltaTime);
    void onExit();

    // Generic call for custom functions
    template<typename... Args>
    void call(const std::string& functionName, Args&&... args) {
        sol::function func = lua[functionName];
        if (func.valid()) {
            func(std::forward<Args>(args)...);
        }
    }

    sol::state& getState();

private:
    sol::state lua;
    GameWorld* gameWorld;

    void registerBindings();
};
