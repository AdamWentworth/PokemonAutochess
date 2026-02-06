#pragma once

#include <memory>

class GameContext;
class InputEvent;

namespace game {

/**
 * Holds the running game instance (world/state/systems/ui/logging).
 * Constructed by GameBootstrap; driven by GameRuntime.
 *
 * Design goal: this can later be constructed in tests with a fake/Headless GameContext.
 */
class GameSession {
public:
    explicit GameSession(GameContext& ctx);
    ~GameSession();

    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    GameSession(GameSession&&) noexcept;
    GameSession& operator=(GameSession&&) noexcept;

    void handleEvent(const InputEvent& event);
    void fixedUpdate(float dt);
    void render(int drawableW, int drawableH);
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace game
