// GameState.h

#pragma once
#include <SDL2/SDL.h>

class GameState {
public:
    virtual ~GameState() {}
    virtual void onEnter() = 0;
    virtual void onExit() = 0;
    virtual void handleInput(SDL_Event& event) = 0;
    virtual void update(float deltaTime) = 0;
    virtual void render() = 0;
};
