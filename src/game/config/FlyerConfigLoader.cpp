// FlyerConfigLoader.cpp
#include "FlyerConfigLoader.h"
#include "game/logging/LoggerUtil.h"

#include <nlohmann/json.hpp>
#include <fstream>

#include <algorithm>
#include <cctype>

static std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

FlyerConfigLoader& FlyerConfigLoader::getInstance() {
    static FlyerConfigLoader inst;
    return inst;
}

bool FlyerConfigLoader::loadConfig(const std::string& path, LogBus::Logger* logger) {
    std::ifstream f(path);
    if (!f) {
        game::log::error(logger, std::string("[FlyerConfigLoader] Could not open: ") + path);
        flyers.clear();
        airDefaults.clear();
        return false;
    }

    nlohmann::json j;
    try {
        f >> j;
    } catch (...) {
        game::log::error(logger, std::string("[FlyerConfigLoader] Failed to parse JSON: ") + path);
        flyers.clear();
        airDefaults.clear();
        return false;
    }

    std::unordered_set<std::string> nextFlyers;
    std::unordered_map<std::string, AirLocomotionDefaults> nextDefaults;

    if (j.contains("flyers") && j["flyers"].is_array()) {
        for (const auto& v : j["flyers"]) {
            if (!v.is_string()) continue;
            const std::string name = toLowerCopy(v.get<std::string>());
            if (!name.empty()) nextFlyers.insert(name);
        }
    }

    // Optional: per-species air-locomotion defaults.
    // Schema:
    // "airLocomotionDefaults": { "pidgey": { "airLiftY": 0.65, ... }, ... }
    if (j.contains("airLocomotionDefaults") && j["airLocomotionDefaults"].is_object()) {
        const auto& obj = j["airLocomotionDefaults"];
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            const std::string species = toLowerCopy(it.key());
            if (species.empty() || !it.value().is_object()) continue;

            AirLocomotionDefaults d;
            const auto& o = it.value();

            if (o.contains("airLiftY") && o["airLiftY"].is_number()) {
                d.airLiftY = o["airLiftY"].get<float>();
            }
            if (o.contains("takeoffSec") && o["takeoffSec"].is_number()) {
                d.takeoffSec = o["takeoffSec"].get<float>();
            }
            if (o.contains("landingSec") && o["landingSec"].is_number()) {
                d.landingSec = o["landingSec"].get<float>();
            }
            if (o.contains("takeoffAnimSpeed") && o["takeoffAnimSpeed"].is_number()) {
                d.takeoffAnimSpeed = o["takeoffAnimSpeed"].get<float>();
            }
            if (o.contains("landAnimSpeed") && o["landAnimSpeed"].is_number()) {
                d.landAnimSpeed = o["landAnimSpeed"].get<float>();
            }
            if (o.contains("debugAnimLogs") && o["debugAnimLogs"].is_boolean()) {
                d.debugAnimLogs = o["debugAnimLogs"].get<bool>();
            }

            nextDefaults[species] = std::move(d);
        }
    }

    flyers.swap(nextFlyers);
    airDefaults.swap(nextDefaults);

    game::log::info(logger, std::string("[FlyerConfigLoader] Loaded flyers: ") + std::to_string(flyers.size()) +
                 " airLocomotionDefaults: " + std::to_string(airDefaults.size()));
    return true;
}

bool FlyerConfigLoader::isFlyer(const std::string& speciesName) const {
    if (speciesName.empty()) return false;
    return flyers.find(toLowerCopy(speciesName)) != flyers.end();
}

const FlyerConfigLoader::AirLocomotionDefaults*
FlyerConfigLoader::getAirLocomotionDefaults(const std::string& speciesName) const {
    if (speciesName.empty()) return nullptr;
    const std::string key = toLowerCopy(speciesName);
    auto it = airDefaults.find(key);
    if (it == airDefaults.end()) return nullptr;
    return &it->second;
}
