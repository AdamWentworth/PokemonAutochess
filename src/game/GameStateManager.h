// GameStateManager.h
#pragma once
#include <memory>
#include <stack>
#include "GameState.h"

struct InputEvent;

class GameStateManager {
public:
    void pushState(std::unique_ptr<GameState> state);
    void popState();
    GameState* getCurrentState();
    void handleInput(const InputEvent& event);
    void update(float deltaTime);
    void render();

private:
    std::stack<std::unique_ptr<GameState>> stateStack;
};


