// src/game/config/LeechSeedConfigDB.h
#pragma once

#include <string>

struct LeechSeedConfig {
    float sapPercent = 0.05f;     // percent of target max HP per tick
    float tickIntervalSec = 2.0f; // seconds between saps
    float durationSec = 6.0f;     // total duration
    int   minSap = 1;             // minimum sap amount per tick
    float healMultiplier = 1.0f;  // 1.0 = heal equal to sap
};

class LeechSeedConfigDB {
public:
    static LeechSeedConfigDB& get();

    // Safe to call multiple times; loads once.
    bool ensureLoaded(const std::string& path = "config/leech_seed_config.cfg");

    const LeechSeedConfig& getConfig() const { return cfg; }

private:
    LeechSeedConfigDB() = default;

    bool loaded = false;
    LeechSeedConfig cfg{};
};
