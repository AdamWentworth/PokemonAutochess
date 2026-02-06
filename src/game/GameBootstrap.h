#pragma once

#include <memory>

class GameContext;

namespace game {

/**
 * Composition root: loads configs, wires services/systems, and returns a ready-to-run session.
 * Goal: keep GameRuntime free of config/singleton wiring so loop/orchestration is isolated.
 */
class GameSession;

class GameBootstrap {
public:
    static std::unique_ptr<GameSession> create(GameContext& ctx);
};

} // namespace game
