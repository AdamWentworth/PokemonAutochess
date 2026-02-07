// MovesConfigLoader.cpp
#include "MovesConfigLoader.h"
#include "game/logging/LoggerUtil.h"

#include "game/config/JsonFile.h"

bool MovesConfigLoader::loadConfig(const std::string& filePath, LogBus::Logger* logger) {
    nlohmann::json j;
    if (!ConfigIO::loadJsonFile(filePath, j, "MovesConfigLoader", /*silentIfMissing=*/false, logger)) {
        return false;
    }

    if (!j.is_object()) {
        game::log::error(logger, std::string("[MovesConfigLoader] Root must be an object: ") + filePath);
        return false;
    }

    moves_.clear();

    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string name = it.key();
        const auto& m = it.value();
        if (!m.is_object()) {
            game::log::warn(logger, std::string("[MovesConfigLoader] Skipping non-object move '") + name + "'");
            continue;
        }

        MoveData md;
        md.name = name;
        md.type = m.value("type", "");
        md.kind = m.value("kind", "fast");
        md.cooldownSec = m.value("cooldownSec", 0.5f);
        md.power = m.value("power", 0);
        md.range = m.value("range", 1.5f);
        md.energyGain = m.value("energyGain", 0);
        md.energyCost = m.value("energyCost", 0);

        if (m.contains("status") && m["status"].is_object()) {
            const auto& s = m["status"];
            md.status.valid       = true;
            md.status.effect      = s.value("effect", "");
            md.status.magnitude   = s.value("magnitude", 0.0f);
            md.status.durationSec = s.value("durationSec", 0.0f);
            md.status.target      = s.value("target", "");
        }

        moves_[name] = std::move(md);
    }

    game::log::info(logger, std::string("[MovesConfigLoader] Loaded ") + std::to_string(moves_.size()) + " moves");
    return true;
}

const MoveData* MovesConfigLoader::getMove(const std::string& name) const {
    auto it = moves_.find(name);
    return (it == moves_.end()) ? nullptr : &it->second;
}
