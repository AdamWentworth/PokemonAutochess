// GameState.h

#pragma once

struct InputEvent;

class GameState {
public:
    virtual ~GameState() {}
    virtual void onEnter() = 0;
    virtual void onExit() = 0;
    virtual void handleInput(const InputEvent& event) = 0;
    virtual void update(float deltaTime) = 0;
    virtual void render() = 0;
};


