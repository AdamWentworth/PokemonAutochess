// GameConfig.cpp

#include "GameConfig.h"

#include "engine/core/Paths.h"
#include "game/logging/LoggerUtil.h"

#include <sol/sol.hpp>

namespace {
void applyFontPathDefaults(GameConfigData& cfg) {
    // If the font path is still the literal "assets/..." default,
    // route it through the asset helper so PAC_ASSET_ROOT is respected.
    if (cfg.fontPath == "assets/fonts/GillSans.ttf") {
        cfg.fontPath = engine::paths::asset("fonts/GillSans.ttf");
    }
}
}

GameConfigData GameConfig::load(LogBus::Logger* logger) {
    GameConfigData cfg;
    cfg.loadSource = engine::paths::data("scripts/config/game.lua");
    applyFontPathDefaults(cfg);

    sol::state L;
    L.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string);

    sol::load_result chunk = L.load_file(cfg.loadSource);
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
    }

    applyFontPathDefaults(cfg);

    cfg.loadOk = true;
    cfg.loadError.clear();
    return cfg;
}
