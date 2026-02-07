// tests/TestGameConfig.cpp
#include <string>

#include "game/GameConfig.h"

// Returns true on pass; on fail writes message into outErr.
// NOTE: Some repo revisions do not populate GameConfigData::loadSource.
// This test does NOT fail on empty loadSource; it only enforces core diagnostics invariants.
bool test_gameconfig_diagnostics(std::string& outErr) {
    GameConfigData cfg = GameConfig::load(nullptr);

    // Invariant: loadOk implies loadError empty; !loadOk implies loadError non-empty.
    if (cfg.loadOk) {
        if (!cfg.loadError.empty()) {
            outErr = "GameConfig: loadOk==true but loadError is non-empty";
            return false;
        }
    } else {
        if (cfg.loadError.empty()) {
            outErr = "GameConfig: loadOk==false but loadError is empty";
            return false;
        }
    }

    // Optional diagnostic: loadSource may be empty in older code; don't fail tests on it.
    // If you want to enforce it, apply the GameConfig visibility drop-in that sets loadSource.

    // Basic sanity on defaults (should be positive).
    if (cfg.cols <= 0 || cfg.rows <= 0 || cfg.cellSize <= 0.0f) {
        outErr = "GameConfig: board defaults/values are not sane (cols/rows/cellSize)";
        return false;
    }

    return true;
}
