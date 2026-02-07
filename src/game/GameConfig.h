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

namespace LogBus { class Logger; }

class GameConfig {
public:
    // Loads from scripts/config/game.lua (no caching).
    // Behavior: returns defaults on failure, but records diagnostics.
    static GameConfigData load(LogBus::Logger* logger = nullptr);
};
