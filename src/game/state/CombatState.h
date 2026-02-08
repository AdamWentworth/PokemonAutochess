#pragma once

#include "game/GameState.h"
#include "game/GameServices.h"
#include "game/scripting/LuaScript.h"

#include <memory>
#include <string>

class GameStateManager;
class GameWorld;

class TextRenderer;

class CombatState : public GameState {
public:
    CombatState(GameStateManager* manager, GameWorld* world, GameServices& services, const std::string& scriptPath);
    ~CombatState() override;

    void onEnter() override;
    void onExit() override;
    void handleInput(const InputEvent& event) override;
    void update(float deltaTime) override;
    void render() override;

private:
    GameStateManager* stateManager = nullptr;
    GameWorld* gameWorld = nullptr;
    GameServices& services;

    LuaScript script;

    std::unique_ptr<TextRenderer> textRenderer;
    std::string combatMessage;

    const std::string& scriptPath() const;
};
