// src/game/runtime/GameRuntime.h
#pragma once

#include <memory>

// Forward declarations to keep this header light.
struct GameContext;
struct InputEvent;

/*
    GameRuntime:
    - PIMPL hides heavy engine/game headers from all includers.
    - IMPORTANT (MSVC): destructor is out-of-line so std::unique_ptr<Impl> can delete a complete type.
*/
class GameRuntime {
public:
    GameRuntime();
    ~GameRuntime();                       // out-of-line in .cpp (required)

    GameRuntime(GameRuntime&&) noexcept;
    GameRuntime& operator=(GameRuntime&&) noexcept;

    GameRuntime(const GameRuntime&) = delete;
    GameRuntime& operator=(const GameRuntime&) = delete;

    void init(GameContext& ctx);
    void handleEvent(const InputEvent& event);
    void fixedUpdate(float dt);
    void render(int drawableW, int drawableH);
    void shutdown();

private:
    struct Impl;                          // defined in .cpp
    std::unique_ptr<Impl> impl_;
};

