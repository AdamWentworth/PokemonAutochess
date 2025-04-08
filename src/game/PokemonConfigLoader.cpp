// PokemonConfigLoader.cpp

#include "PokemonConfigLoader.h"
#include <fstream>
#include <iostream>

PokemonConfigLoader& PokemonConfigLoader::getInstance() {
    static PokemonConfigLoader instance;
    return instance;
}

bool PokemonConfigLoader::loadConfig(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "[PokemonConfigLoader] Failed to open: " << filePath << "\n";
        return false;
    }

    nlohmann::json jsonData;
    file >> jsonData;

    for (const auto& [name, data] : jsonData.items()) {
        PokemonStats stats;
        stats.hp = data.value("hp", 100);
        stats.attack = data.value("attack", 10);
        stats.model = data.value("model", name + ".glb");
        statsMap[name] = stats;
    }

    std::cout << "[PokemonConfigLoader] Loaded stats for " << statsMap.size() << " Pokémon\n";
    return true;
}

const PokemonStats* PokemonConfigLoader::getStats(const std::string& name) const {
    auto it = statsMap.find(name);
    if (it != statsMap.end()) {
        return &it->second;
    }
    return nullptr;
}
