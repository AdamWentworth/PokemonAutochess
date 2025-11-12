// GameConfig.cpp

#include "GameConfig.h"
#include <sol/sol.hpp>
#include <iostream>

const GameConfigData& GameConfig::get() {
    static GameConfigData cfg;
    static bool inited = false;
    if (!inited) {
        sol::state L;
        L.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string);

        sol::load_result chunk = L.load_file("scripts/config/game.lua");
        if (!chunk.valid()) {
            sol::error e = chunk;
            std::cerr << "[GameConfig] Failed to load game.lua: " << e.what() << "\n";
        } else {
            sol::protected_function_result r = chunk();
            if (!r.valid()) {
                sol::error e = r;
                std::cerr << "[GameConfig] Failed to execute game.lua: " << e.what() << "\n";
            } else {
                sol::table t = r;
                if (t.valid()) {
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
                    // NEW: leveling
                    sol::table leveling = t["leveling"];
                    if (leveling.valid()) {
                        cfg.baseLevel     = leveling.get_or("base_level", cfg.baseLevel);
                        cfg.perLevelBoost = leveling.get_or("per_level_boost", cfg.perLevelBoost);
                    }
                }
            }
        }
        inited = true;
    }
    return cfg;
}
