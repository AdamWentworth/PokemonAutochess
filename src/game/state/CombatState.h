// src/game/state/CombatState.h
#pragma once
#include "../GameState.h"
#include "../GameWorld.h"
#include "../LuaScript.h"
#include "../../engine/ui/TextRenderer.h"
#include <memory>

class GameStateManager;
class GameWorld;

class CombatState : public GameState {
public:
    CombatState(GameStateManager* manager, GameWorld* world, const std::string& scriptPath);
    ~CombatState();

    void onEnter() override;
    void onExit() override;
    void handleInput(SDL_Event& event) override;
    void update(float deltaTime) override;
    void render() override;

private:
    GameStateManager* stateManager;
    GameWorld* gameWorld;
    std::unique_ptr<TextRenderer> textRenderer;
    LuaScript script;
    std::string combatMessage;
};