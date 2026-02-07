#include "game/GameBootstrap.h"

#include <iostream>
#include <filesystem>
#include <utility>

#include "engine/core/GameContext.h"
#include "engine/core/Paths.h"

#include "game/GameSession.h"
#include "game/assets/DevAssetStore.h"
#include "game/assets/PackedAssetStore.h"
#include "game/config/GameDataDb.h"
#include "game/logging/LogBus.h"
#include "game/logging/LoggerUtil.h"

namespace game {

std::unique_ptr<GameSession> GameBootstrap::create(GameContext& ctx) {
    // Load configs (game-specific) from data root (PAC_DATA_ROOT), not CWD-sensitive literals.
    LogBus::Logger bootstrapLog;
    std::unique_ptr<engine::IAssetStore> dataStore;
    const std::string packPath = engine::paths::dataPack();
    if (!packPath.empty()) {
        auto pack = std::make_unique<assets::PackedAssetStore>();
        std::string err;
        if (pack->open(packPath, &err)) {
            dataStore = std::move(pack);
            game::log::info(&bootstrapLog, std::string("[Init] Using packed data bundle: ") + packPath);
        } else {
            game::log::warn(&bootstrapLog, std::string("[Init] Failed to open pack: ") + packPath +
                (err.empty() ? "" : (" (" + err + ")")));
        }
    }
    if (!dataStore) {
        dataStore = std::make_unique<assets::DevAssetStore>(engine::paths::dataRoot());
    }

    GameDataDb db;
    db.pokemon.loadConfig("config/pokemon_config.json", &bootstrapLog, dataStore.get());
    db.moves.loadConfig("config/moves_config.json", &bootstrapLog, dataStore.get());
    db.attackAnims.loadConfig("config/attack_anim_config.json", &bootstrapLog, dataStore.get());
    db.flyers.loadConfig("config/flyers_config.json", &bootstrapLog, dataStore.get());

    std::cout << "[Init] CWD: " << std::filesystem::current_path() << "\n";
    std::cout << "[Init] PAC_DATA_ROOT: " << engine::paths::dataRoot() << "\n";
    std::cout << "[Init] PAC_ASSET_ROOT: " << engine::paths::assetRoot() << "\n";

    return std::make_unique<GameSession>(ctx, std::move(db));
}

} // namespace game
