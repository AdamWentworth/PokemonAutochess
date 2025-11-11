// LuaScript.h

#pragma once
#include <string>
#include <sol/sol.hpp>

class GameWorld;
class GameStateManager;

class LuaScript {
public:
    explicit LuaScript(GameWorld* world, GameStateManager* manager = nullptr);
    ~LuaScript() = default;

    bool loadScript(const std::string& filePath);

    void onEnter();
    void onUpdate(float deltaTime);
    void onExit();

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
    GameWorld* gameWorld = nullptr;
    GameStateManager* stateManager = nullptr;

    void registerBindings();
};
