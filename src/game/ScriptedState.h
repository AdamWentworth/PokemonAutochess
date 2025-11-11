// ScriptedState.h

#pragma once

#include "game/GameState.h"
#include "game/GameWorld.h"
#include "game/LuaScript.h"
#include "game/systems/CardSystem.h"
#include "engine/ui/TextRenderer.h"

class GameStateManager;

// A thin C++ wrapper that forwards state lifecycle to a Lua script.
// Additionally, if the Lua script exposes starter UI helpers
// (get_starter_cards / on_card_click / handle_starter_key),
// this state will build and drive a simple card UI for selection.
class ScriptedState : public GameState {
public:
    // 'scriptPath' is a Lua file that may implement:
    // on_enter(), on_exit(), handleInput(event), on_update(dt), onRender()
    ScriptedState(GameStateManager* manager, GameWorld* world, const std::string& scriptPath);
    ~ScriptedState() override;

    void onEnter() override;
    void onExit() override;
    void handleInput(SDL_Event& event) override;
    void update(float deltaTime) override;
    void render() override;

private:
    GameStateManager* stateManager = nullptr;
    GameWorld* gameWorld = nullptr;
    std::string scriptPath;

    LuaScript script; // owns its own sol::state, with bindings to gameWorld/stateManager registered

    // --- Starter UI harness (only used when the Lua script provides the hooks) ---
    CardSystem cardSystem;
    std::unique_ptr<TextRenderer> titleText;
    bool uiInitialized = false;

    void ensureStarterUI();
};
