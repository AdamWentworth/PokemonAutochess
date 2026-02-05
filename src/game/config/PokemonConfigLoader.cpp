// PokemonConfigLoader.cpp
#include "PokemonConfigLoader.h"
#include "game/logging/LogBus.h"

#include <fstream>

PokemonConfigLoader& PokemonConfigLoader::getInstance() {
    static PokemonConfigLoader instance;
    return instance;
}

bool PokemonConfigLoader::loadConfig(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        LogBus::error(std::string("[PokemonConfigLoader] Failed to open: ") + filePath);
        return false;
    }

    nlohmann::json jsonData;
    file >> jsonData;

    statsMap.clear();

    for (const auto& [name, data] : jsonData.items()) {
        PokemonStats stats;
        stats.hp             = data.value("hp", 100);
        stats.attack         = data.value("attack", 10);
        stats.movementSpeed  = data.value("movementSpeed", 1.0f);
        stats.model          = data.value("model", name + ".glb");

        // Parse loadoutByLevel
        if (data.contains("loadoutByLevel") && data["loadoutByLevel"].is_object()) {
            for (auto it = data["loadoutByLevel"].begin(); it != data["loadoutByLevel"].end(); ++it) {
                int lvl = std::stoi(it.key());
                const auto& row = it.value();

                LoadoutEntry le;
                if (row.contains("fast") && row["fast"].is_string()) {
                    le.fast = row["fast"].get<std::string>();
                }
                if (row.contains("charged") && row["charged"].is_string()) {
                    le.charged = row["charged"].get<std::string>();
                    le.hasCharged = !le.charged.empty();
                }
                // keep even if fast empty; caller can fallback
                stats.loadoutByLevel[lvl] = le;
            }
        }

        statsMap[name] = std::move(stats);
    }

    LogBus::info(std::string("[PokemonConfigLoader] Loaded stats for ") +
                 std::to_string(statsMap.size()) + " Pokémon");
    return true;
}

const PokemonStats* PokemonConfigLoader::getStats(const std::string& name) const {
    auto it = statsMap.find(name);
    if (it != statsMap.end()) {
        return &it->second;
    }
    return nullptr;
}
