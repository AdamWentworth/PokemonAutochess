// PokemonConfigLoader.cpp
#include "PokemonConfigLoader.h"
#include "game/logging/LoggerUtil.h"
#include "game/config/JsonFile.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace {

std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string normalizeModelScaleMode(std::string value,
                                    const std::string& species,
                                    LogBus::Logger* logger) {
    value = toLowerCopy(std::move(value));
    if (value == "raw") value = "native";  // Backward-compat alias.
    if (value == "native" || value == "normalized") return value;

    game::log::warn(logger, std::string("[PokemonConfigLoader] Invalid modelScaleMode '") +
                           value + "' for species '" + species + "'. Using 'native'.");
    return "native";
}

std::string normalizeModelScaleAxis(std::string value,
                                    const std::string& species,
                                    LogBus::Logger* logger) {
    value = toLowerCopy(std::move(value));
    if (value == "max" || value == "x" || value == "y" || value == "z" || value == "median") return value;

    game::log::warn(logger, std::string("[PokemonConfigLoader] Invalid modelScaleAxis '") +
                           value + "' for species '" + species + "'. Using 'max'.");
    return "max";
}

bool isNativeModelIdentity(const std::string& value) {
    if (value.empty()) return false;
    std::string extension =
        std::filesystem::path(value).extension().string();
    extension = toLowerCopy(std::move(extension));
    return extension == ".phmodel";
}

}  // namespace

std::string PokemonStats::resolveModel(const std::string& variant) const {
    const std::string key = toLowerCopy(variant.empty() ? "regular" : variant);
    if (const auto it = modelVariants.find(key); it != modelVariants.end() && !it->second.empty()) {
        return it->second;
    }
    // Species without visible sex differences can share the shiny asset while
    // retaining a female_shiny identity that becomes meaningful on evolution.
    if (key.find("shiny") != std::string::npos) {
        if (const auto it = modelVariants.find("shiny");
            it != modelVariants.end() && !it->second.empty()) {
            return it->second;
        }
    }
    if (const auto it = modelVariants.find("regular");
        it != modelVariants.end() && !it->second.empty()) {
        return it->second;
    }
    return model;
}

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

    decltype(statsMap) parsedStats;

    for (const auto& [name, data] : jsonData.items()) {
        if (!data.is_object()) {
            game::log::warn(logger, std::string("[PokemonConfigLoader] Skipping non-object entry: ") + name);
            continue;
        }

        PokemonStats stats;
        stats.hp            = data.value("hp", 100);
        stats.attack        = data.value("attack", 10);
        stats.movementSpeed = data.value("movementSpeed", 1.0f);
        stats.visualScale   = std::max(0.05f, data.value("visualScale", 1.0f));
        stats.modelScaleMode = normalizeModelScaleMode(data.value("modelScaleMode", std::string("native")), name, logger);
        stats.modelScaleAxis = normalizeModelScaleAxis(data.value("modelScaleAxis", std::string("max")), name, logger);
        const auto model = data.find("model");
        if (model == data.end() || !model->is_string() ||
            model->get_ref<const std::string&>().empty()) {
            game::log::error(
                logger,
                std::string("[PokemonConfigLoader] Species '") + name +
                    "' requires a non-empty model field naming its canonical .phmodel asset.");
            return false;
        }
        stats.model = model->get<std::string>();
        if (!isNativeModelIdentity(stats.model)) {
            game::log::error(
                logger,
                std::string("[PokemonConfigLoader] Species '") + name +
                    "' model must name a canonical .phmodel asset; runtime GLB, glTF, and .pacmdl Pokemon models are not supported: " +
                    stats.model);
            return false;
        }
        if (data.contains("modelVariants") && data["modelVariants"].is_object()) {
            for (const auto& [variantName, variantValue] : data["modelVariants"].items()) {
                if (!variantValue.is_string()) {
                    game::log::error(
                        logger,
                        std::string("[PokemonConfigLoader] Species '") + name +
                            "' modelVariants." + variantName +
                            " must be a non-empty .phmodel string.");
                    return false;
                }
                const std::string modelName = variantValue.get<std::string>();
                if (!isNativeModelIdentity(modelName)) {
                    game::log::error(
                        logger,
                        std::string("[PokemonConfigLoader] Species '") + name +
                            "' modelVariants." + variantName +
                            " must name a canonical .phmodel asset: " + modelName);
                    return false;
                }
                stats.modelVariants[toLowerCopy(variantName)] = modelName;
            }
        }
        if (stats.modelVariants.find("regular") == stats.modelVariants.end() && !stats.model.empty()) {
            stats.modelVariants.emplace("regular", stats.model);
        }
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

        parsedStats[toLowerCopy(name)] = std::move(stats);
    }

    statsMap = std::move(parsedStats);
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
    const std::string key = toLowerCopy(name);
    auto it = statsMap.find(key);
    if (it != statsMap.end()) return &it->second;
    return nullptr;
}

int PokemonConfigLoader::getBaseExp(const std::string& name) const {
    const std::string key = toLowerCopy(name);
    auto it = baseExpMap.find(key);
    if (it != baseExpMap.end()) return it->second;
    return 0;
}
