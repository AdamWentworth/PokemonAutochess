// GameStateManager.h
#pragma once
#include <memory>
#include <stack>
#include "GameState.h"
#include <SDL2/SDL.h>

class GameStateManager {
public:
    void pushState(std::unique_ptr<GameState> state);
    void popState();
    GameState* getCurrentState();
    void handleInput(SDL_Event& event);
    void update(float deltaTime);
    void render();

private:
    std::stack<std::unique_ptr<GameState>> stateStack;
};
