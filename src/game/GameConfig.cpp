// GameConfig.cpp

#include "GameConfig.h"

#include "engine/core/Paths.h"
#include "engine/core/IAssetStore.h"
#include "game/logging/LoggerUtil.h"

#include <algorithm>
#include <sol/sol.hpp>

namespace {
void applyFontPathDefaults(GameConfigData& cfg) {
    // If the font path is still the literal "assets/..." default,
    // route it through the asset helper so PAC_ASSET_ROOT is respected.
    if (cfg.fontPath == "assets/fonts/GillSans.ttf") {
        cfg.fontPath = engine::paths::asset("fonts/GillSans.ttf");
    }
}

std::string normalizeVirtualPath(std::string path) {
    std::string root = engine::paths::dataRoot();
    std::replace(root.begin(), root.end(), '\\', '/');
    std::replace(path.begin(), path.end(), '\\', '/');
    if (!root.empty() && (root.back() == '/' || root.back() == '\\')) root.pop_back();
    if (!root.empty() && path.rfind(root + "/", 0) == 0) {
        path = path.substr(root.size() + 1);
    }
    while (!path.empty() && (path.front() == '/' || path.front() == '\\')) {
        path.erase(path.begin());
    }
    return path;
}
}

GameConfigData GameConfig::load(LogBus::Logger* logger, const engine::IAssetStore* store) {
    GameConfigData cfg;
    cfg.loadSource = engine::paths::data("scripts/config/game.lua");
    applyFontPathDefaults(cfg);

    sol::state L;
    L.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string);

    sol::load_result chunk;
    if (store) {
        std::string text;
        std::string err;
        const std::string virt = normalizeVirtualPath(cfg.loadSource);
        if (store->readText(virt, text, &err)) {
            chunk = L.load(text);
        } else {
            if (!err.empty()) {
                game::log::warn(logger, std::string("[GameConfig] Asset store read failed: ") + virt + " (" + err + ")");
            }
            chunk = L.load_file(cfg.loadSource);
        }
    } else {
        chunk = L.load_file(cfg.loadSource);
    }
    if (!chunk.valid()) {
        sol::error e = chunk;
        cfg.loadOk = false;
        cfg.loadError = e.what();
        game::log::error(logger, std::string("[GameConfig] Failed to load: ") + cfg.loadSource + " (" + cfg.loadError + ")");
        return cfg;
    }

    sol::protected_function_result r = chunk();
    if (!r.valid()) {
        sol::error e = r;
        cfg.loadOk = false;
        cfg.loadError = e.what();
        game::log::error(logger, std::string("[GameConfig] Failed to execute: ") + cfg.loadSource + " (" + cfg.loadError + ")");
        return cfg;
    }

    sol::table t = r;
    if (!t.valid()) {
        cfg.loadOk = false;
        cfg.loadError = "game.lua did not return a table";
        game::log::error(logger, std::string("[GameConfig] ") + cfg.loadError + ": " + cfg.loadSource);
        return cfg;
    }

    sol::table board = t["board"];
    if (board.valid()) {
        cfg.cols     = board.get_or("cols", cfg.cols);
        cfg.rows     = board.get_or("rows", cfg.rows);
        cfg.cellSize = board.get_or("cellSize", cfg.cellSize);
    }

    sol::table bench = t["bench"];
    if (bench.valid()) {
        cfg.benchSlots = bench.get_or("slots", cfg.benchSlots);
    }

    sol::table fonts = t["fonts"];
    if (fonts.valid()) {
        sol::table ui = fonts["ui"];
        if (ui.valid()) {
            cfg.fontPath = ui.get_or("path", cfg.fontPath);
            cfg.fontSize = ui.get_or("size", cfg.fontSize);
        }
    }

    sol::table leveling = t["leveling"];
    if (leveling.valid()) {
        cfg.baseLevel     = leveling.get_or("base_level", cfg.baseLevel);
        cfg.perLevelBoost = leveling.get_or("per_level_boost", cfg.perLevelBoost);

        // Optional per-stat overrides (fall back to perLevelBoost if unspecified).
        sol::optional<float> hpBoost = leveling["per_level_hp_boost"];
        sol::optional<float> atkBoost = leveling["per_level_attack_boost"];
        sol::optional<float> spdBoost = leveling["per_level_speed_boost"];

        cfg.perLevelHpBoost = hpBoost ? *hpBoost : cfg.perLevelBoost;
        cfg.perLevelAttackBoost = atkBoost ? *atkBoost : cfg.perLevelBoost;
        cfg.perLevelSpeedBoost = spdBoost ? *spdBoost : cfg.perLevelBoost;
    }

    sol::table xp = t["xp"];
    if (xp.valid()) {
        cfg.xpPerFaint    = xp.get_or("per_faint", cfg.xpPerFaint);
        cfg.xpLevelBase   = xp.get_or("level_base", cfg.xpLevelBase);
        cfg.xpLevelGrowth = xp.get_or("level_growth", cfg.xpLevelGrowth);
        cfg.xpMaxLevel    = xp.get_or("max_level", cfg.xpMaxLevel);
        cfg.xpYieldMult   = xp.get_or("yield_mult", cfg.xpYieldMult);
    }

    sol::table faint = t["faint"];
    if (faint.valid()) {
        cfg.faintFadeSec = faint.get_or("fade_sec", cfg.faintFadeSec);
        cfg.faintBlockTiles = faint.get_or("block_tile", cfg.faintBlockTiles);
    }

    sol::table economy = t["economy"];
    if (economy.valid()) {
        cfg.startingCash = economy.get_or("starting_cash", cfg.startingCash);
    }

    sol::table items = t["items"];
    if (items.valid()) {
        cfg.potionHealPct = items.get_or("potion_heal_pct", cfg.potionHealPct);
        cfg.potionHealFlat = items.get_or("potion_heal_flat", cfg.potionHealFlat);
    }

    sol::table capture = t["capture"];
    if (capture.valid()) {
        cfg.captureAttemptSec = capture.get_or("attempt_sec", cfg.captureAttemptSec);
        cfg.captureMinChance = capture.get_or("min_chance", cfg.captureMinChance);
        cfg.captureMaxChance = capture.get_or("max_chance", cfg.captureMaxChance);
        cfg.captureHpFactorMin = capture.get_or("hp_factor_min", cfg.captureHpFactorMin);
        cfg.captureHpFactorMax = capture.get_or("hp_factor_max", cfg.captureHpFactorMax);
        cfg.captureFaintBonus = capture.get_or("faint_bonus", cfg.captureFaintBonus);
        cfg.captureBallScale = capture.get_or("ball_scale", cfg.captureBallScale);
        cfg.captureBallScaleStart = capture.get_or("ball_scale_start", cfg.captureBallScaleStart);
    }

    applyFontPathDefaults(cfg);

    cfg.loadOk = true;
    cfg.loadError.clear();
    return cfg;
}
