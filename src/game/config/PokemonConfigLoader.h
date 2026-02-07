// PokemonConfigLoader.h
#pragma once

#include <string>
#include <unordered_map>
#include <map>

#include <nlohmann/json.hpp>

namespace LogBus { class Logger; }

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

    // loadoutByLevel[level] -> LoadoutEntry
    std::map<int, LoadoutEntry> loadoutByLevel;
};

class PokemonConfigLoader {
public:
    static PokemonConfigLoader& getInstance();
    bool loadConfig(const std::string& filePath, LogBus::Logger* logger = nullptr);

    const PokemonStats* getStats(const std::string& name) const;

public:
    PokemonConfigLoader() = default;

private:
    std::unordered_map<std::string, PokemonStats> statsMap;
};
