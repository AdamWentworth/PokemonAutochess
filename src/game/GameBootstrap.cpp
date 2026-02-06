#include "game/GameBootstrap.h"

#include <iostream>
#include <filesystem>

#include "engine/core/GameContext.h"
#include "engine/core/Paths.h"

#include "game/GameSession.h"

// Config & data
#include "game/GameConfig.h"
#include "game/config/PokemonConfigLoader.h"
#include "game/config/MovesConfigLoader.h"
#include "game/config/AttackAnimConfigLoader.h"
#include "game/config/FlyerConfigLoader.h"

namespace game {

std::unique_ptr<GameSession> GameBootstrap::create(GameContext& ctx) {
    // Load configs (game-specific) from data root (PAC_DATA_ROOT), not CWD-sensitive literals.
    PokemonConfigLoader::getInstance().loadConfig(engine::paths::data("config/pokemon_config.json"));
    MovesConfigLoader::getInstance().loadConfig(engine::paths::data("config/moves_config.json"));
    AttackAnimConfigLoader::getInstance().loadConfig(engine::paths::data("config/attack_anim_config.json"));
    FlyerConfigLoader::getInstance().loadConfig(engine::paths::data("config/flyers_config.json"));

    std::cout << "[Init] CWD: " << std::filesystem::current_path() << "\n";
    std::cout << "[Init] PAC_DATA_ROOT: " << engine::paths::dataRoot() << "\n";
    std::cout << "[Init] PAC_ASSET_ROOT: " << engine::paths::assetRoot() << "\n";

    return std::make_unique<GameSession>(ctx);
}

} // namespace game
