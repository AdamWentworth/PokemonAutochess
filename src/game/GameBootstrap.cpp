#include "game/GameBootstrap.h"

#include <iostream>
#include <filesystem>
#include <utility>

#include "engine/core/GameContext.h"
#include "engine/core/Paths.h"

#include "game/GameSession.h"
#include "game/config/GameDataDb.h"
#include "game/logging/LogBus.h"

namespace game {

std::unique_ptr<GameSession> GameBootstrap::create(GameContext& ctx) {
    // Load configs (game-specific) from data root (PAC_DATA_ROOT), not CWD-sensitive literals.
    LogBus::Logger bootstrapLog;
    GameDataDb db;
    db.pokemon.loadConfig(engine::paths::data("config/pokemon_config.json"), &bootstrapLog);
    db.moves.loadConfig(engine::paths::data("config/moves_config.json"), &bootstrapLog);
    db.attackAnims.loadConfig(engine::paths::data("config/attack_anim_config.json"), &bootstrapLog);
    db.flyers.loadConfig(engine::paths::data("config/flyers_config.json"), &bootstrapLog);

    std::cout << "[Init] CWD: " << std::filesystem::current_path() << "\n";
    std::cout << "[Init] PAC_DATA_ROOT: " << engine::paths::dataRoot() << "\n";
    std::cout << "[Init] PAC_ASSET_ROOT: " << engine::paths::assetRoot() << "\n";

    return std::make_unique<GameSession>(ctx, std::move(db));
}

} // namespace game
