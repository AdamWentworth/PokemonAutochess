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

    assets::DevAssetStore devFallbackStore(engine::paths::dataRoot());
    const bool usingPackStore = dynamic_cast<assets::PackedAssetStore*>(dataStore.get()) != nullptr;

    auto loadWithFallback = [&](const char* label, auto&& loadFn) {
        bool loaded = loadFn(dataStore.get());
        if (!loaded && usingPackStore) {
            if (loadFn(&devFallbackStore)) {
                game::log::warn(&bootstrapLog, std::string("[Init] Falling back to dev data for ") + label +
                    " (packed data missing or stale)");
                loaded = true;
            }
        } else if (loaded && usingPackStore) {
            // In dev workflows, always prefer filesystem configs over packaged copies
            // so tuning changes apply immediately without repacking content.
            if (loadFn(&devFallbackStore)) {
                game::log::info(&bootstrapLog, std::string("[Init] Overlayed dev data for ") + label);
            }
        }

        if (!loaded) {
            game::log::error(&bootstrapLog, std::string("[Init] Failed to load ") + label);
        }
        return loaded;
    };

    GameDataDb db;
    loadWithFallback("pokemon_config.json",
        [&](const engine::IAssetStore* store) {
            return db.pokemon.loadConfig("config/pokemon_config.json", &bootstrapLog, store);
        });
    loadWithFallback("pokemon_base_exp.json",
        [&](const engine::IAssetStore* store) {
            return db.pokemon.applyBaseExpConfig("config/pokemon_base_exp.json", &bootstrapLog, store);
        });
    loadWithFallback("moves_config.json",
        [&](const engine::IAssetStore* store) {
            return db.moves.loadConfig("config/moves_config.json", &bootstrapLog, store);
        });
    loadWithFallback("attack_anim_config.json",
        [&](const engine::IAssetStore* store) {
            return db.attackAnims.loadConfig("config/attack_anim_config.json", &bootstrapLog, store);
        });
    loadWithFallback("flyers_config.json",
        [&](const engine::IAssetStore* store) {
            return db.flyers.loadConfig("config/flyers_config.json", &bootstrapLog, store);
        });
    loadWithFallback("evolution_config.json",
        [&](const engine::IAssetStore* store) {
            return db.evolution.loadConfig("config/evolution_config.json", &bootstrapLog, store);
        });
    if (db.evolution.ruleCount() == 0) {
        game::log::warn(&bootstrapLog, "[Init] No evolution rules loaded (evolution_config.json)");
    }

    std::cout << "[Init] CWD: " << std::filesystem::current_path() << "\n";
    std::cout << "[Init] PAC_DATA_ROOT: " << engine::paths::dataRoot() << "\n";
    std::cout << "[Init] PAC_ASSET_ROOT: " << engine::paths::assetRoot() << "\n";
    std::cout << "[Init] pokemon_config path: "
              << std::filesystem::absolute(engine::paths::data("config/pokemon_config.json")) << "\n";

    return std::make_unique<GameSession>(ctx, std::move(db));
}

} // namespace game
