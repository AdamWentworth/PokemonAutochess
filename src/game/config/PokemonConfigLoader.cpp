// PokemonConfigLoader.cpp
#include "PokemonConfigLoader.h"
#include "game/logging/LoggerUtil.h"
#include "game/config/JsonFile.h"

#include <string>

bool PokemonConfigLoader::loadConfig(const std::string& filePath,
                                     LogBus::Logger* logger,
                                     const engine::IAssetStore* store) {
    nlohmann::json jsonData;
    if (!ConfigIO::loadJsonFile(filePath, jsonData, "PokemonConfigLoader", /*silentIfMissing=*/false, logger, store)) {
        return false;
    }

    if (!jsonData.is_object()) {
        game::log::error(logger, std::string("[PokemonConfigLoader] Root must be an object: ") + filePath);
        return false;
    }

    statsMap.clear();

    for (const auto& [name, data] : jsonData.items()) {
        if (!data.is_object()) {
            game::log::warn(logger, std::string("[PokemonConfigLoader] Skipping non-object entry: ") + name);
            continue;
        }

        PokemonStats stats;
        stats.hp            = data.value("hp", 100);
        stats.attack        = data.value("attack", 10);
        stats.movementSpeed = data.value("movementSpeed", 1.0f);
        stats.model         = data.value("model", name + ".glb");

        if (data.contains("loadoutByLevel") && data["loadoutByLevel"].is_object()) {
            const auto& lob = data["loadoutByLevel"];
            for (auto it = lob.begin(); it != lob.end(); ++it) {
                const std::string levelKey = it.key();
                const auto& row = it.value();

                int lvl = 0;
                try {
                    lvl = std::stoi(levelKey);
                } catch (...) {
                    game::log::warn(logger, std::string("[PokemonConfigLoader] Invalid level key '") + levelKey +
                                 "' for species '" + name + "'; skipping.");
                    continue;
                }

                LoadoutEntry le;
                if (row.is_object()) {
                    if (row.contains("fast") && row["fast"].is_string()) {
                        le.fast = row["fast"].get<std::string>();
                    }
                    if (row.contains("charged") && row["charged"].is_string()) {
                        le.charged = row["charged"].get<std::string>();
                        le.hasCharged = !le.charged.empty();
                    }
                } else {
                    game::log::warn(logger, std::string("[PokemonConfigLoader] loadoutByLevel[") + levelKey +
                                 "] must be an object for species '" + name + "'.");
                }

                stats.loadoutByLevel[lvl] = le;
            }
        }

        statsMap[name] = std::move(stats);
    }

    game::log::info(logger, std::string("[PokemonConfigLoader] Loaded ") + std::to_string(statsMap.size()) + " species");
    return true;
}

const PokemonStats* PokemonConfigLoader::getStats(const std::string& name) const {
    auto it = statsMap.find(name);
    if (it != statsMap.end()) return &it->second;
    return nullptr;
}
