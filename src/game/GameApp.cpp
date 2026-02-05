// src/game/GameApp.cpp

#include "game/GameApp.h"
#include "game/GameRuntime.h"

GameApp::GameApp() = default;
GameApp::~GameApp() = default;

void GameApp::init(GameContext& ctx) {
    runtime = std::make_unique<GameRuntime>();
    runtime->init(ctx);
}

void GameApp::handleEvent(const InputEvent& event) {
    if (runtime) runtime->handleEvent(event);
}

void GameApp::fixedUpdate(float dt) {
    if (runtime) runtime->fixedUpdate(dt);
}

void GameApp::render(int drawableW, int drawableH) {
    if (runtime) runtime->render(drawableW, drawableH);
}

void GameApp::shutdown() {
    if (runtime) {
        runtime->shutdown();
        runtime.reset();
    }
}
