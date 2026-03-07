// GameStateManager.cpp

#include "GameStateManager.h"

#include <chrono>

void GameStateManager::pushState(std::unique_ptr<GameState> state) {
    if (!state) return;
    if (inUpdate) {
        PendingOp op;
        op.type = PendingType::Push;
        op.state = std::move(state);
        pendingOps.push_back(std::move(op));
        return;
    }

    state->onEnter();
    stateStack.push(std::move(state));
}

void GameStateManager::popState() {
    if (inUpdate) {
        PendingOp op;
        op.type = PendingType::Pop;
        pendingOps.push_back(std::move(op));
        return;
    }
    if (!stateStack.empty()) {
        stateStack.top()->onExit();
        stateStack.pop();
    }
}

void GameStateManager::clearAndPushState(std::unique_ptr<GameState> state) {
    if (!state) return;
    if (inUpdate) {
        PendingOp op;
        op.type = PendingType::ClearAndPush;
        op.state = std::move(state);
        pendingOps.push_back(std::move(op));
        return;
    }

    while (!stateStack.empty()) {
        stateStack.top()->onExit();
        stateStack.pop();
    }
    state->onEnter();
    stateStack.push(std::move(state));
}

GameState* GameStateManager::getCurrentState() {
    return stateStack.empty() ? nullptr : stateStack.top().get();
}

void GameStateManager::handleInput(const InputEvent& event) {
    inUpdate = true;
    if (GameState* state = getCurrentState()) {
        state->handleInput(event);
    }
    inUpdate = false;
    flushPending();
}

void GameStateManager::update(float deltaTime) {
    using Clock = std::chrono::high_resolution_clock;

    lastUpdateTiming_ = {};
    inUpdate = true;
    const auto stateUpdateStart = Clock::now();
    if (GameState* state = getCurrentState()) {
        state->update(deltaTime);
    }
    lastUpdateTiming_.stateUpdateMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(Clock::now() - stateUpdateStart).count());
    inUpdate = false;

    const auto flushStart = Clock::now();
    flushPending();
    lastUpdateTiming_.flushPendingMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(Clock::now() - flushStart).count());
}

void GameStateManager::render() {
    if (GameState* state = getCurrentState()) {
        state->render();
    }
}

void GameStateManager::flushPending() {
    if (pendingOps.empty()) return;

    for (auto& op : pendingOps) {
        if (op.type == PendingType::Pop) {
            if (!stateStack.empty()) {
                stateStack.top()->onExit();
                stateStack.pop();
            }
        } else if (op.type == PendingType::Push) {
            if (op.state) {
                op.state->onEnter();
                stateStack.push(std::move(op.state));
            }
        } else if (op.type == PendingType::ClearAndPush) {
            while (!stateStack.empty()) {
                stateStack.top()->onExit();
                stateStack.pop();
            }
            if (op.state) {
                op.state->onEnter();
                stateStack.push(std::move(op.state));
            }
        }
    }
    pendingOps.clear();
}


