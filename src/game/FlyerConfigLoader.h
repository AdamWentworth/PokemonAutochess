// src/game/FlyerConfigLoader.h
#pragma once

#include <string>
#include <unordered_set>

// Loads a simple list of species names that should use FlightLocomotion.
//
// config/flyers_config.json:
// {
//   "flyers": ["pidgey", "spearow"]
// }
//
// Names are matched case-insensitively.
class FlyerConfigLoader {
public:
    static FlyerConfigLoader& getInstance();

    // Safe to call multiple times; replaces the current set on success.
    bool loadConfig(const std::string& path);

    // Case-insensitive check.
    bool isFlyer(const std::string& speciesName) const;

    int getFlyerCount() const { return (int)flyers.size(); }

private:
    FlyerConfigLoader() = default;

    std::unordered_set<std::string> flyers; // stored lowercase
};


