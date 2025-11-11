// src/game/state/CombatState.h

#pragma once
#include "../GameState.h"
#include "../LuaScript.h"
#include "../../engine/ui/TextRenderer.h"
#include <memory>

class MovementSystem;

class CombatState : public GameState {
public:
    CombatState(GameStateManager* manager, GameWorld* world, const std::string& scriptPath);
    ~CombatState() override;

    void onEnter() override;
    void onExit() override;
    void handleInput(SDL_Event& event) override;
    void update(float deltaTime) override;
    void render() override;

private:
    GameStateManager* stateManager;
    GameWorld* gameWorld;
    LuaScript script;

    std::unique_ptr<TextRenderer> textRenderer;
    std::unique_ptr<MovementSystem> movementSystem;

    std::string combatMessage;

    // REMOVE this member if it exists:
    // GridOccupancy gridOccupancy;
};
