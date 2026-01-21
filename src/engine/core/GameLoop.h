// src/engine/core/GameLoop.h
#pragma once

#include <SDL2/SDL.h>

struct GameContext;

/*
    GameLoop:
    - Engine-facing interface for the game/app layer.
    - Application drives the game via these callbacks.
    - Uses non-const SDL_Event& because some existing game code expects SDL_Event&.
*/
class GameLoop {
public:
    virtual ~GameLoop() = default;

    virtual void init(GameContext& ctx) = 0;
    virtual void handleEvent(SDL_Event& event) = 0;

    virtual void fixedUpdate(float dt) = 0;
    virtual void render(int drawableW, int drawableH) = 0;

    virtual void shutdown() = 0;
};
