// GameStateManager.cpp

#include "GameStateManager.h"

void GameStateManager::pushState(std::unique_ptr<GameState> state) {
    if (state) {
        state->onEnter();
        stateStack.push(std::move(state));
    }
}

void GameStateManager::popState() {
    if (!stateStack.empty()) {
        stateStack.top()->onExit();
        stateStack.pop();
    }
}

GameState* GameStateManager::getCurrentState() {
    return stateStack.empty() ? nullptr : stateStack.top().get();
}

void GameStateManager::handleInput(SDL_Event& event) {
    if (GameState* state = getCurrentState()) {
        state->handleInput(event);
    }
}

void GameStateManager::update(float deltaTime) {
    if (GameState* state = getCurrentState()) {
        state->update(deltaTime);
    }
}

void GameStateManager::render() {
    if (GameState* state = getCurrentState()) {
        state->render();
    }
}
