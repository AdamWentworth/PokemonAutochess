// src/game/GameApp.h
#pragma once

#include <memory>

#include "engine/core/GameLoop.h"

// Forward decls (engine)
struct GameContext;
struct InputEvent;

// Runtime (game)
struct GameRuntime;

/*
    GameApp (Game):
    - Thin adapter implementing GameLoop.
    - Owns GameRuntime which holds the actual game state, systems, and UI.

    IMPORTANT (MSVC):
    - Constructor + destructor are out-of-line so std::unique_ptr can hold forward-declared types.
*/
class GameApp final : public GameLoop {
public:
    GameApp();
    ~GameApp() override;

    void init(GameContext& ctx) override;
    void handleEvent(const InputEvent& event) override;

    void fixedUpdate(float dt) override;
    void render(int drawableW, int drawableH) override;

    void shutdown() override;

private:
    std::unique_ptr<GameRuntime> runtime;
};
