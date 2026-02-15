// PokemonConfigLoader.cpp
#include "PokemonConfigLoader.h"
#include "game/logging/LoggerUtil.h"
#include "game/config/JsonFile.h"

#include <algorithm>
#include <cctype>
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

    auto toLowerCopy = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        return s;
    };

        PokemonStats stats;
        stats.hp            = data.value("hp", 100);
        stats.attack        = data.value("attack", 10);
        stats.movementSpeed = data.value("movementSpeed", 1.0f);
        stats.visualScale   = std::max(0.05f, data.value("visualScale", 1.0f));
        stats.modelScaleMode = toLowerCopy(data.value("modelScaleMode", std::string("native")));
        // Backward-compat alias.
        if (stats.modelScaleMode == "raw") stats.modelScaleMode = "native";
        if (stats.modelScaleMode != "native" && stats.modelScaleMode != "normalized") {
            game::log::warn(logger, std::string("[PokemonConfigLoader] Invalid modelScaleMode '") +
                                   stats.modelScaleMode + "' for species '" + name +
                                   "'. Using 'native'.");
            stats.modelScaleMode = "native";
        }
        stats.modelScaleAxis = toLowerCopy(data.value("modelScaleAxis", std::string("max")));
        if (stats.modelScaleAxis != "max" &&
            stats.modelScaleAxis != "x" &&
            stats.modelScaleAxis != "y" &&
            stats.modelScaleAxis != "z" &&
            stats.modelScaleAxis != "median") {
            game::log::warn(logger, std::string("[PokemonConfigLoader] Invalid modelScaleAxis '") +
                                   stats.modelScaleAxis + "' for species '" + name +
                                   "'. Using 'max'.");
            stats.modelScaleAxis = "max";
        }
        stats.model         = data.value("model", name + ".glb");
        stats.baseExp       = data.value("baseExp", stats.baseExp);
        stats.catchRate     = data.value("catchRate", stats.catchRate);
        stats.shopBaseCost  = std::max(1, data.value("shopBaseCost", stats.shopBaseCost));

        if (data.contains("types")) {
            const auto& t = data["types"];
            if (t.is_string()) {
                stats.types.push_back(toLowerCopy(t.get<std::string>()));
            } else if (t.is_array()) {
                for (const auto& entry : t) {
                    if (entry.is_string()) {
                        stats.types.push_back(toLowerCopy(entry.get<std::string>()));
                    }
                }
            }
        }

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

        statsMap[toLowerCopy(name)] = std::move(stats);
    }

    game::log::info(logger, std::string("[PokemonConfigLoader] Loaded ") + std::to_string(statsMap.size()) + " species");
    return true;
}

bool PokemonConfigLoader::applyBaseExpConfig(const std::string& filePath,
                                             LogBus::Logger* logger,
                                             const engine::IAssetStore* store) {
    nlohmann::json jsonData;
    if (!ConfigIO::loadJsonFile(filePath, jsonData, "PokemonBaseExp", /*silentIfMissing=*/true, logger, store)) {
        return false;
    }

    if (!jsonData.is_object()) {
        game::log::warn(logger, std::string("[PokemonBaseExp] Root must be an object: ") + filePath);
        return false;
    }

    auto toLowerCopy = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        return s;
    };

    baseExpMap.clear();
    for (const auto& [name, data] : jsonData.items()) {
        if (!data.is_number_integer()) continue;
        const int val = data.get<int>();
        const std::string key = toLowerCopy(name);
        baseExpMap[key] = val;
        auto it = statsMap.find(key);
        if (it != statsMap.end()) {
            it->second.baseExp = val;
        }
    }

    game::log::info(logger, std::string("[PokemonBaseExp] Loaded ") + std::to_string(baseExpMap.size()) + " entries");
    return true;
}

const PokemonStats* PokemonConfigLoader::getStats(const std::string& name) const {
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    auto it = statsMap.find(key);
    if (it != statsMap.end()) return &it->second;
    return nullptr;
}

int PokemonConfigLoader::getBaseExp(const std::string& name) const {
    std::string key = name;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    auto it = baseExpMap.find(key);
    if (it != baseExpMap.end()) return it->second;
    return 0;
}
