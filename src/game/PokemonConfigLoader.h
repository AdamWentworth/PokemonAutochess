// PokemonConfigLoader.h
#pragma once

#include <string>
#include <unordered_map>
#include <map>
#include <../../third_party/nlohmann/json.hpp>

struct LoadoutEntry {
    std::string fast;     // move name
    std::string charged;  // move name (optional)
    bool hasCharged = false;
};

struct PokemonStats {
    int hp = 100;
    int attack = 10;
    float movementSpeed = 1.0f;
    std::string model;

    // NEW: loadoutByLevel[level] -> LoadoutEntry
    std::map<int, LoadoutEntry> loadoutByLevel;
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
