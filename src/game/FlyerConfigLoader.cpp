// src/game/FlyerConfigLoader.cpp
#include "FlyerConfigLoader.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>

static std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

FlyerConfigLoader& FlyerConfigLoader::getInstance() {
    static FlyerConfigLoader inst;
    return inst;
}

bool FlyerConfigLoader::loadConfig(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "[FlyerConfigLoader] Could not open: " << path << "\n";
        flyers.clear();
        return false;
    }

    nlohmann::json j;
    try {
        f >> j;
    } catch (...) {
        std::cerr << "[FlyerConfigLoader] Failed to parse JSON: " << path << "\n";
        flyers.clear();
        return false;
    }

    std::unordered_set<std::string> next;

    if (j.contains("flyers") && j["flyers"].is_array()) {
        for (const auto& v : j["flyers"]) {
            if (!v.is_string()) continue;
            const std::string name = toLowerCopy(v.get<std::string>());
            if (!name.empty()) next.insert(name);
        }
    }

    flyers.swap(next);
    std::cout << "[FlyerConfigLoader] Loaded flyers: " << flyers.size() << "\n";
    return true;
}

bool FlyerConfigLoader::isFlyer(const std::string& speciesName) const {
    if (speciesName.empty()) return false;
    return flyers.find(toLowerCopy(speciesName)) != flyers.end();
}


