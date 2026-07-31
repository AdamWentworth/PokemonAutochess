#pragma once

#include <memory>
#include <string>

struct GameContext;
struct InputEvent;

struct GameDataDb;

namespace game {

/**
 * Holds the running game instance (world/state/systems/ui/logging).
 * Constructed by GameBootstrap; driven by GameRuntime.
 *
 * Design goal: can be constructed in tests with a fake/Headless GameContext.
 */
class GameSession {
public:
    GameSession(GameContext& ctx, GameDataDb db);
    ~GameSession();

    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    GameSession(GameSession&&) noexcept;
    GameSession& operator=(GameSession&&) noexcept;

    void handleEvent(const InputEvent& event);
    void fixedUpdate(float dt);
    void render(int drawableW, int drawableH);
    bool activateEditorPreview(
        const std::string& state,
        const std::string& gameMode,
        const std::string& snapshotPath,
        std::string* outError = nullptr);
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace game
