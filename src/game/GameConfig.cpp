// GameConfig.cpp
#include "GameConfig.h"

#include "engine/core/Paths.h"

#include <sol/sol.hpp>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

static bool envTruthy(const char* v) {
    if (!v) return false;
    const std::string s(v);
    return (s == "1" || s == "true" || s == "TRUE" || s == "yes" || s == "YES");
}

static bool loadLuaConfig(GameConfigData& cfg, std::string& outErr) {
    outErr.clear();

    // If the default font path is still the literal "assets/.." string,
    // route it through the asset helper so PAC_ASSET_ROOT is respected.
    if (cfg.fontPath == "assets/fonts/GillSans.ttf") {
        cfg.fontPath = engine::paths::asset("fonts/GillSans.ttf");
    }

    sol::state L;
    L.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string);

    const std::string gameLua = engine::paths::data("scripts/config/game.lua");
    cfg.loadSource = gameLua;

    sol::load_result chunk = L.load_file(gameLua);
    if (!chunk.valid()) {
        sol::error e = chunk;
        outErr = std::string("load_file failed: ") + e.what();
        return false;
    }

    sol::protected_function_result r = chunk();
    if (!r.valid()) {
        sol::error e = r;
        outErr = std::string("execution failed: ") + e.what();
        return false;
    }

    sol::table t = r;
    if (!t.valid()) {
        outErr = "config script did not return a table";
        return false;
    }

    // board
    sol::table board = t["board"];
    if (board.valid()) {
        cfg.cols     = board.get_or("cols", cfg.cols);
        cfg.rows     = board.get_or("rows", cfg.rows);
        cfg.cellSize = board.get_or("cellSize", cfg.cellSize);
    }

    // bench
    sol::table bench = t["bench"];
    if (bench.valid()) {
        cfg.benchSlots = bench.get_or("slots", cfg.benchSlots);
    }

    // fonts
    sol::table fonts = t["fonts"];
    if (fonts.valid()) {
        sol::table ui = fonts["ui"];
        if (ui.valid()) {
            cfg.fontPath = ui.get_or("path", cfg.fontPath);
            cfg.fontSize = ui.get_or("size", cfg.fontSize);
        }
    }

    // leveling
    sol::table leveling = t["leveling"];
    if (leveling.valid()) {
        cfg.baseLevel     = leveling.get_or("base_level", cfg.baseLevel);
        cfg.perLevelBoost = leveling.get_or("per_level_boost", cfg.perLevelBoost);
    }

    return true;
}

} // namespace

const GameConfigData& GameConfig::get() {
    static GameConfigData cfg;
    static bool inited = false;

    if (!inited) {
        cfg.loadOk = true;
        cfg.loadError.clear();
        cfg.loadSource.clear();

        std::string err;
        const bool ok = loadLuaConfig(cfg, err);
        cfg.loadOk = ok;
        cfg.loadError = ok ? std::string() : err;

        if (!ok) {
            std::cerr << "[GameConfig] Failed to load '" << cfg.loadSource
                      << "': " << cfg.loadError << "\n";

            // Optional strict mode for CI/dev: crash early instead of silently using defaults.
            // Set either:
            //   PAC_STRICT_CONFIG=1   (recommended)
            //   or GAME_STRICT_CONFIG=1
            if (envTruthy(std::getenv("PAC_STRICT_CONFIG")) ||
                envTruthy(std::getenv("GAME_STRICT_CONFIG"))) {
                std::cerr << "[GameConfig] Strict mode enabled; aborting.\n";
                std::abort();
            }
        }

        inited = true;
    }

    return cfg;
}

void GameConfig::reload() {
    // Re-run initialization by resetting the internal guard.
    // Implemented by reassigning via a local static indirection.
    // Simple and safe for this codebase's current usage (startup-time config).
    struct ResetHelper {
        static void reset() {
            // Force re-init by touching get() static guard via function-local statics trick.
            // We can't directly reset function-local statics, so we simulate reload by
            // re-executing the load and mutating the cached cfg in place.
            GameConfigData& cfg = const_cast<GameConfigData&>(GameConfig::get());
            cfg.loadOk = true;
            cfg.loadError.clear();
            cfg.loadSource.clear();

            std::string err;
            const bool ok = loadLuaConfig(cfg, err);
            cfg.loadOk = ok;
            cfg.loadError = ok ? std::string() : err;

            if (!ok) {
                std::cerr << "[GameConfig] Reload failed '" << cfg.loadSource
                          << "': " << cfg.loadError << "\n";
            }
        }
    };

    ResetHelper::reset();
}
