// PokemonConfigLoader.h

#pragma once

#include <string>
#include <unordered_map>
#include <../../third_party/nlohmann/json.hpp>

struct PokemonStats {
    int hp = 100;
    int attack = 10;
    std::string model;
};

class PokemonConfigLoader {
public:
    static PokemonConfigLoader& getInstance();
    bool loadConfig(const std::string& filePath);

    const PokemonStats* getStats(const std::string& name) const;

private:
    PokemonConfigLoader() = default;

    std::unordered_map<std::string, PokemonStats> statsMap;
};
