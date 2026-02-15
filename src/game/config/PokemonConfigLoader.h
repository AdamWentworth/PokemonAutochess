// PokemonConfigLoader.h
#pragma once

#include <string>
#include <unordered_map>
#include <map>
#include <vector>

#include <nlohmann/json.hpp>

namespace LogBus { class Logger; }
namespace engine { class IAssetStore; }

struct LoadoutEntry {
    std::string fast;     // move name
    std::string charged;  // move name (optional)
    bool hasCharged = false;
};

struct PokemonStats {
    int hp = 100;
    int attack = 10;
    float movementSpeed = 1.0f;
    float visualScale = 1.0f;
    // "native" (default): keep Blender/exported model size (no importer normalization effect).
    // "normalized": keep importer normalization (legacy compatibility).
    std::string modelScaleMode = "native";
    // Optional normalization axis used to compensate for problematic model bounds.
    // Supported: "max" (default), "x", "y", "z", "median".
    std::string modelScaleAxis = "max";
    std::string model;
    std::vector<std::string> types;
    int baseExp = 50;
    float catchRate = 0.5f;
    int shopBaseCost = 1;

    // loadoutByLevel[level] -> LoadoutEntry
    std::map<int, LoadoutEntry> loadoutByLevel;
};

class PokemonConfigLoader {
public:
    bool loadConfig(const std::string& filePath,
                    LogBus::Logger* logger = nullptr,
                    const engine::IAssetStore* store = nullptr);
    bool applyBaseExpConfig(const std::string& filePath,
                            LogBus::Logger* logger = nullptr,
                            const engine::IAssetStore* store = nullptr);

    const PokemonStats* getStats(const std::string& name) const;
    const std::unordered_map<std::string, PokemonStats>& all() const { return statsMap; }
    int getBaseExp(const std::string& name) const;

public:
    PokemonConfigLoader() = default;

private:
    std::unordered_map<std::string, PokemonStats> statsMap;
    std::unordered_map<std::string, int> baseExpMap;
};
