// src/game/FlyerConfigLoader.h
#pragma once

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <optional>

namespace LogBus { class Logger; }

// Loads a list of species names that should use FlightLocomotion,
// plus OPTIONAL per-species defaults for the visual-only air locomotion tuning.
//
// config/flyers_config.json (backwards compatible):
// {
//   "flyers": ["pidgey", "spearow"],
//   "airLocomotionDefaults": {
//     "pidgey": { "airLiftY": 0.65 }
//   }
// }
//
// Notes:
// - Names are matched case-insensitively.
// - Defaults are only applied when the animset meta does NOT specify the same value.
// - This loader intentionally keeps "what species needs what tuning" in config (not code).

class FlyerConfigLoader {
public:
    struct AirLocomotionDefaults {
        std::optional<float> airLiftY;          // world-space Y offset when airborne
        std::optional<float> takeoffSec;        // clip-time seconds
        std::optional<float> landingSec;        // total landing clip-time seconds (A+B+C)
        std::optional<float> takeoffAnimSpeed;  // playback multiplier
        std::optional<float> landAnimSpeed;     // playback multiplier
        std::optional<bool>  debugAnimLogs;     // enables logs for this species (optional)
    };

    static FlyerConfigLoader& getInstance();

    // Safe to call multiple times; replaces the current data on success.
    bool loadConfig(const std::string& path, LogBus::Logger* logger = nullptr);

    // Case-insensitive check.
    bool isFlyer(const std::string& speciesName) const;

    // Optional air-locomotion defaults for a given species (case-insensitive).
    // Returns nullptr when no defaults exist.
    const AirLocomotionDefaults* getAirLocomotionDefaults(const std::string& speciesName) const;

    int getFlyerCount() const { return (int)flyers.size(); }
    int getDefaultsCount() const { return (int)airDefaults.size(); }

public:
    FlyerConfigLoader() = default;

private:
    std::unordered_set<std::string> flyers; // stored lowercase
    std::unordered_map<std::string, AirLocomotionDefaults> airDefaults; // key: lowercase species
};

