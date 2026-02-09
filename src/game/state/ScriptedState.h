#pragma once

#include <memory>
#include <string>

#include "game/GameState.h"
#include "game/GameWorld.h"
#include "game/GameServices.h"
#include "game/scripting/LuaScript.h"
#include "game/systems/CardSystem.h"
#include "engine/ui/TextRenderer.h"

class GameStateManager;

// A thin C++ wrapper that forwards state lifecycle to a Lua script.
// Additionally, if the Lua script exposes starter UI helpers
// (get_starter_cards / on_card_click / handle_starter_key),
// this state will build and drive a simple card UI for selection.
class ScriptedState : public GameState {
public:
    ScriptedState(GameStateManager* manager, GameWorld* world, GameServices& services, const std::string& scriptPath);

    ~ScriptedState() override;

    void onEnter() override;
    void onExit() override;
    void handleInput(const InputEvent& event) override;
    void update(float deltaTime) override;
    void render() override;

private:
    void ensureCardUI();
    void rebuildCardRow();

private:
    GameStateManager* stateManager = nullptr;
    GameWorld* gameWorld = nullptr;
    GameServices& services;

    std::string scriptPath;
    LuaScript script;

    CardSystem cardSystem;
    std::unique_ptr<TextRenderer> titleText;
    bool uiInitialized = false;
    enum class CardMode { None, Starter, Shop };
    CardMode cardMode = CardMode::None;
};
