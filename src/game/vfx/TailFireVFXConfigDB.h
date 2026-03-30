// src/game/vfx/TailFireVFXConfigDB.h
#pragma once

#include <string>
#include <unordered_map>

#include "TailFireVFXConfig.h"

class TailFireVFXConfigDB {
public:
    static TailFireVFXConfigDB& get();

    // Safe to call multiple times; loads once.
    bool ensureLoaded(const std::string& path = "config/tail_fire_config.cfg");

    // Applies overrides for a species key (use lowercase).
    void applyIfAny(const std::string& speciesLower, TailFireVFXConfig& io) const;

private:
    TailFireVFXConfigDB() = default;

    struct Entry {
        bool has = false;
        TailFireVFXConfig cfg;
    };

    bool loaded = false;
    std::unordered_map<std::string, Entry> entries;
};
