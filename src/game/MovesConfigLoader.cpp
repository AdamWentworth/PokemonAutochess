// MovesConfigLoader.cpp
#include "MovesConfigLoader.h"
#include <fstream>
#include <iostream>

MovesConfigLoader& MovesConfigLoader::getInstance() {
    static MovesConfigLoader inst;
    return inst;
}

bool MovesConfigLoader::loadConfig(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[MovesConfigLoader] Failed to open: " << filePath << "\n";
        return false;
    }
    nlohmann::json j;
    file >> j;

    moves_.clear();
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string name = it.key();
        const auto& m = it.value();

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
    std::cout << "[MovesConfigLoader] Loaded " << moves_.size() << " moves\n";
    return true;
}

const MoveData* MovesConfigLoader::getMove(const std::string& name) const {
    auto it = moves_.find(name);
    return (it == moves_.end()) ? nullptr : &it->second;
}
