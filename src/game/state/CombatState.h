#pragma once

#include "game/GameState.h"
#include "game/scripting/LuaScript.h"

#include <memory>
#include <string>

class GameStateManager;
class GameWorld;
struct GameServices;

class MovementSystem;
class CombatSystem;
class TextRenderer;

class CombatState : public GameState {
public:
    CombatState(GameStateManager* manager, GameWorld* world, const std::string& scriptPath);
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
    GameServices* services = nullptr;

    LuaScript script;

    std::unique_ptr<TextRenderer> textRenderer;
    std::unique_ptr<MovementSystem> movementSystem;
    std::unique_ptr<CombatSystem>  combatSystem;

    std::string combatMessage;

    const std::string& scriptPath() const;
};
