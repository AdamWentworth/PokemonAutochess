#pragma once

#include <memory>
#include <string>

#include "game/GameState.h"
#include "game/GameWorld.h"
#include "game/GameConfig.h"        // GameConfigData
#include "game/scripting/LuaScript.h"
#include "game/systems/CardSystem.h"
#include "engine/ui/TextRenderer.h"

class GameStateManager;
struct GameServices;

// A thin C++ wrapper that forwards state lifecycle to a Lua script.
// Additionally, if the Lua script exposes starter UI helpers
// (get_starter_cards / on_card_click / handle_starter_key),
// this state will build and drive a simple card UI for selection.
class ScriptedState : public GameState {
public:
    // Preferred: explicitly pass services (removes GameConfig::get() usage in this state).
    ScriptedState(GameStateManager* manager, GameWorld* world, GameServices& services, const std::string& scriptPath);

    // Back-compat: older call sites that don't have services yet.
    // Keeps the project compiling while you migrate call sites.
    ScriptedState(GameStateManager* manager, GameWorld* world, const std::string& scriptPath);

    ~ScriptedState() override;

    void onEnter() override;
    void onExit() override;
    void handleInput(const InputEvent& event) override;
    void update(float deltaTime) override;
    void render() override;

private:
    const GameConfigData& cfg() const;

    void ensureStarterUI();

private:
    GameStateManager* stateManager = nullptr;
    GameWorld* gameWorld = nullptr;
    GameServices* services = nullptr;

    std::string scriptPath;
    LuaScript script;

    CardSystem cardSystem;
    std::unique_ptr<TextRenderer> titleText;
    bool uiInitialized = false;
};
