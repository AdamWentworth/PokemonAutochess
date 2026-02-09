// GameStateManager.h
#pragma once
#include <memory>
#include <stack>
#include <vector>
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
    bool inUpdate = false;

    enum class PendingType { Push, Pop };
    struct PendingOp {
        PendingType type = PendingType::Pop;
        std::unique_ptr<GameState> state;
    };
    std::vector<PendingOp> pendingOps;

    void flushPending();
};


