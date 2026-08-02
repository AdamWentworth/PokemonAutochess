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
    int benchGapCells = 0;

    std::string fontPath = "assets/fonts/GillSans.ttf";
    int fontSize = 48;

    // leveling
    int baseLevel = 1;
    float perLevelBoost = 0.08f; // 8% default
    float perLevelHpBoost = 0.08f;
    float perLevelAttackBoost = 0.08f;
    float perLevelSpeedBoost = 0.08f;

    // experience
    int xpPerFaint = 10;
    int xpLevelBase = 10;
    float xpLevelGrowth = 1.35f;
    int xpMaxLevel = 0; // 0 = no cap
    float xpYieldMult = 1.0f;

    // economy
    int startingCash = 3000;
    int classicStartingGold = 1;
    int classicBaseIncome = 5;
    int classicInterestPer10 = 1;
    int classicInterestCap = 5;
    int classicStreakBonus2To3 = 1;
    int classicStreakBonus4To5 = 2;
    int classicStreakBonus6Plus = 3;

    // items (tuning)
    float potionHealPct = 0.30f; // % of max HP
    int   potionHealFlat = 0;

    // capture tuning
    float captureAttemptSec = 0.75f;
    float captureMinChance = 0.05f;
    float captureMaxChance = 0.95f;
    float captureHpFactorMin = 0.40f; // high HP
    float captureHpFactorMax = 1.00f; // low HP
    float captureFaintBonus = 1.25f;
    float captureBallScale = 1.6f;
    float captureBallScaleStart = 3.0f;

    // fainting visuals + tile blocking
    float faintFadeSec = 0.35f;
    bool  faintBlockTiles = false;

    // ---- New: load diagnostics (non-breaking additions) ----
    bool loadOk = true;                // false if config failed to load/execute/parse
    std::string loadSource;            // resolved path attempted
    std::string loadError;             // human-readable error (empty if ok)
};

namespace LogBus { class Logger; }
namespace engine { class IAssetStore; }

class GameConfig {
public:
    // Loads from scripts/config/game.lua (no caching).
    // Behavior: returns defaults on failure, but records diagnostics.
    static GameConfigData load(LogBus::Logger* logger = nullptr,
                               const engine::IAssetStore* store = nullptr);
};
