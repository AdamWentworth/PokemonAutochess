#pragma once

#include <memory>

struct GameContext;

namespace game {

class GameSession;

/**
 * Composition root: loads configs and wires runtime dependencies.
 * Owns "global" initialization so GameSession and GameRuntime stay testable.
 */
class GameBootstrap {
public:
    static std::unique_ptr<GameSession> create(GameContext& ctx);
};

} // namespace game
