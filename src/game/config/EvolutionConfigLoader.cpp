// EvolutionConfigLoader.cpp
#include "EvolutionConfigLoader.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

#include "game/config/JsonFile.h"
#include "game/logging/LoggerUtil.h"

bool EvolutionConfigLoader::loadConfig(const std::string& filePath,
                                       LogBus::Logger* logger,
                                       const engine::IAssetStore* store) {
    nlohmann::json j;
    if (!ConfigIO::loadJsonFile(filePath, j, "EvolutionConfigLoader", /*silentIfMissing=*/false, logger, store)) {
        return false;
    }

    if (!j.is_object()) {
        game::log::error(logger, std::string("[EvolutionConfigLoader] Root must be an object: ") + filePath);
        return false;
    }

    auto toLowerCopy = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };

    rules_.clear();

    for (const auto& [from, node] : j.items()) {
        if (!node.is_object()) {
            game::log::warn(logger, std::string("[EvolutionConfigLoader] Skipping non-object entry: ") + from);
            continue;
        }

        const std::string to = node.value("evolves_to", std::string());
        const int level = node.value("level", 0);
        if (to.empty() || level <= 0) {
            game::log::warn(logger, std::string("[EvolutionConfigLoader] Invalid rule for '") + from +
                "' (requires evolves_to + positive level).");
            continue;
        }

        EvolutionRule r;
        r.evolvesTo = toLowerCopy(to);
        r.level = level;
        rules_[toLowerCopy(from)] = std::move(r);
    }

    game::log::info(logger, std::string("[EvolutionConfigLoader] Loaded ") + std::to_string(rules_.size()) + " rules");
    return true;
}

const EvolutionRule* EvolutionConfigLoader::getRule(const std::string& species) const {
    std::string key = species;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto it = rules_.find(key);
    if (it == rules_.end()) return nullptr;
    return &it->second;
}
