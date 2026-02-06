// GameConfig.h
#pragma once

#include <string>

// NOTE: This is a drop-in extension of the existing config.
// Existing fields keep their defaults and behavior.
// New fields and helpers make load failures visible to callers and CI.

struct GameConfigData {
    int cols = 8;
    int rows = 8;
    float cellSize = 1.2f;
    int benchSlots = 8;

    std::string fontPath = "assets/fonts/GillSans.ttf";
    int fontSize = 48;

    // leveling
    int baseLevel = 1;
    float perLevelBoost = 0.08f; // 8% default

    // ---- New: load diagnostics (non-breaking additions) ----
    bool loadOk = true;                // false if config failed to load/execute/parse
    std::string loadSource;            // resolved path attempted
    std::string loadError;             // human-readable error (empty if ok)
};

class GameConfig {
public:
    // Loads once from scripts/config/game.lua and caches.
    // Behavior: still returns defaults on failure, but records diagnostics.
    static const GameConfigData& get();

    // New: observability helpers.
    static bool loadedOk() { return get().loadOk; }
    static const std::string& error() { return get().loadError; }
    static const std::string& source() { return get().loadSource; }

    // New: force a reload (useful in dev tools / tests).
    // If you don't need it, ignore it.
    static void reload();
};
