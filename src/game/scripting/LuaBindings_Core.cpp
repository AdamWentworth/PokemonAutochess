// src/game/scripting/LuaBindings.cpp
#include <glm/glm.hpp>
#include "engine/render/Model.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

#include "LuaBindings.h"

#include "game/GameWorld.h"
#include "game/PokemonInstance.h"
#include "game/GameStateManager.h"
#include "game/GameConfig.h"
#include "game/scripting/ScriptAPI.h"
#include "game/scripting/ScriptEventBus.h"

#include "game/animation/FlightLocomotion.h"
#include "game/animation/AttackAnimDebug.h"

#include "game/config/PokemonConfigLoader.h"
#include "game/config/MovesConfigLoader.h"
#include "game/config/AttackAnimConfigLoader.h"
#include "game/config/AnimSetLoader.h"

#include "game/state/scripted/ScriptedState.h"

#include "game/logging/LoggerUtil.h"
#include "game/logging/DebugTrace.h"

#include "LuaBindings_Internal.h"

void registerLuaBindings_Core(sol::state& lua, ScriptAPI& api) {
    LogBus::Logger* logger = &api.logger();
    // Basic enums
    lua.new_enum("PokemonSide",
        "Player", PokemonSide::Player,
        "Enemy",  PokemonSide::Enemy
    );

    // ---- Logging: Lua -> BattleFeed ----
    lua.set_function("emit", [&api](const std::string& tag_or_msg, sol::optional<std::string> payload) {
        api.emit(tag_or_msg, payload ? std::optional<std::string>(*payload) : std::nullopt);
    });
    lua.set_function("emit_catch", [&api](const std::string& msg) {
        api.emitCatch(msg);
    });
    lua.set_function("emit_gold", [&api](const std::string& msg) {
        api.emitGold(msg);
    });

    // ---- Engine-safe spawners ----
    lua.set_function("spawnPokemon", [&api](std::string name, float x, float y, float z) {
        api.spawnPokemon(name, x, y, z);
    });
    lua.set_function("spawn_on_bench", [&api](std::string name, sol::optional<int> level) {
        api.addToBench(name, level.value_or(-1));
    });
    lua.set_function("spawn_on_grid",
    [&api](std::string name, int col, int row, std::string side, sol::optional<int> level) {
        int lvl = level.value_or(-1);
        api.spawnOnGrid(name, col, row, sideFromString(side), lvl);
    });

    // ---- Round events ----
    // Deprecated: legacy shim kept so existing scripts don't crash.
    // Round phase changes are handled directly in C++ (GameApp / RoundSystem).
    lua.set_function("emit_round_phase_changed",
        [](const std::string& prev, const std::string& next) {
            (void)prev;
            (void)next;
            // Intentionally a silent no-op. Phase transitions are handled in C++.
        }
    );

    // ---- Script event stream ----
    lua.set_function("events_drain", [&api, &lua]() {
        sol::state_view L(lua);
        sol::table arr = L.create_table();
        auto events = api.drainEvents();
        int i = 1;
        for (const auto& e : events) {
            sol::table t = L.create_table();
            t["type"] = e.type;
            if (e.hasPayload) t["payload"] = e.payload;
            arr[i++] = t;
        }
        return arr;
    });

    // ---- State mgmt ----
    lua.set_function("push_state", [&api](const std::string& scriptPath) {
        api.pushState(scriptPath);
    });
    lua.set_function("push_combat_state", [&api](const std::string& scriptPath) {
        api.pushCombatState(scriptPath);
    });
    lua.set_function("pop_state", [&api]() { api.popState(); });

    // ---- Economy + items ----
    lua.set_function("get_money", [&api]() {
        return api.getMoney();
    });
    lua.set_function("add_money", [&api](int amount) {
        api.addMoney(amount);
    });
    lua.set_function("spend_money", [&api](int amount) {
        return api.spendMoney(amount);
    });
    lua.set_function("get_item_count", [&api](const std::string& item) {
        return api.getItemCount(item);
    });
    lua.set_function("add_item", [&api](const std::string& item, sol::optional<int> amount) {
        api.addItem(item, amount.value_or(1));
    });
    lua.set_function("consume_item", [&api](const std::string& item, sol::optional<int> amount) {
        return api.consumeItem(item, amount.value_or(1));
    });
    lua.set_function("get_pokemon_catch_rate", [&api](const std::string& name) {
        return api.getPokemonCatchRate(name);
    });
    lua.set_function("get_game_mode", [&api]() {
        return api.getGameMode();
    });
    lua.set_function("set_game_mode", [&api](const std::string& mode) {
        api.setGameMode(mode);
    });
    lua.set_function("get_has_started_game", [&api]() {
        return api.getHasStartedGame();
    });
    lua.set_function("set_has_started_game", [&api](bool started) {
        api.setHasStartedGame(started);
    });
    lua.set_function("set_video_mode", [&api](int width, int height, bool fullscreen) {
        return api.setVideoMode(width, height, fullscreen);
    });
    lua.set_function("get_video_mode", [&api, &lua]() {
        auto vm = api.getVideoMode();
        sol::state_view L(lua);
        sol::table t = L.create_table();
        t["width"] = vm.width;
        t["height"] = vm.height;
        t["fullscreen"] = vm.fullscreen;
        return t;
    });
    lua.set_function("get_renderer_backend_pref", [&api]() {
        return api.getRendererBackendPreference();
    });
    lua.set_function("set_renderer_backend_pref", [&api](const std::string& backend) {
        return api.setRendererBackendPreference(backend);
    });
    lua.set_function("get_vsync_pref", [&api]() {
        return api.getVSyncPreference();
    });
    lua.set_function("set_vsync_pref", [&api](bool enabled) {
        return api.setVSyncPreference(enabled);
    });
    lua.set_function("get_require_discrete_gpu_pref", [&api]() {
        return api.getRequireDiscreteGpuPreference();
    });
    lua.set_function("set_require_discrete_gpu_pref", [&api](bool required) {
        return api.setRequireDiscreteGpuPreference(required);
    });
    lua.set_function("get_active_renderer_backend", [&api]() {
        return api.getActiveRendererBackend();
    });
    lua.set_function("get_active_gpu_renderer", [&api]() {
        return api.getActiveGpuRenderer();
    });
    lua.set_function("get_gpu_adapters", [&api, &lua]() {
        sol::state_view L(lua);
        sol::table arr = L.create_table();
        const auto adapters = api.getGpuAdapters();
        int i = 1;
        for (const auto& adapter : adapters) {
            arr[i++] = adapter;
        }
        return arr;
    });
    lua.set_function("get_preferred_gpu_adapter_pref", [&api]() {
        return api.getPreferredGpuAdapterPreference();
    });
    lua.set_function("set_preferred_gpu_adapter_pref", [&api](const std::string& adapterName) {
        return api.setPreferredGpuAdapterPreference(adapterName);
    });
    lua.set_function("get_character_inking_pref", [&api]() {
        return api.getCharacterInkingPreference();
    });
    lua.set_function("set_character_inking_pref", [&api](bool enabled) {
        return api.setCharacterInkingPreference(enabled);
    });
    lua.set_function("is_active_gpu_discrete", [&api]() {
        return api.isActiveGpuDiscrete();
    });
    lua.set_function("request_restart_to_menu", [&api](const std::string& menuScreen) {
        return api.requestRestartToMenu(menuScreen);
    });
    lua.set_function("consume_boot_menu_screen", [&api]() {
        return api.consumeBootMenuScreen();
    });
    lua.set_function("request_quit", [&api]() {
        api.requestQuit();
    });
    lua.set_function("start_new_game", [&api](const std::string& mode) {
        api.startNewGame(mode);
    });
    lua.set_function("classic_award_round_income", [&api, &lua](bool won) {
        const auto r = api.awardClassicRoundIncome(won);
        sol::state_view L(lua);
        sol::table t = L.create_table();
        t["base"] = r.baseIncome;
        t["interest"] = r.interestIncome;
        t["streak"] = r.streakIncome;
        t["total"] = r.totalIncome;
        t["win_streak"] = r.winStreak;
        t["loss_streak"] = r.lossStreak;
        t["round"] = r.roundIndex;
        t["won"] = r.won;
        return t;
    });
    lua.set_function("classic_shop_get_cards", [&api, &lua]() {
        sol::state_view L(lua);
        sol::table arr = L.create_table();
        const auto cards = api.getClassicShopCards();
        int i = 1;
        for (const auto& c : cards) {
            sol::table row = L.create_table();
            row["name"] = c.name;
            row["level"] = c.level;
            row["cost"] = c.cost;
            arr[i++] = row;
        }
        return arr;
    });
    lua.set_function("classic_shop_set_cards", [&api](sol::table rows) {
        std::vector<ScriptAPI::ClassicShopCardSnapshot> cards;
        for (auto&& kv : rows) {
            if (kv.second.get_type() != sol::type::table) continue;
            sol::table row = kv.second.as<sol::table>();
            ScriptAPI::ClassicShopCardSnapshot c;
            c.name = row.get_or("name", std::string());
            c.level = row.get_or("level", 1);
            c.cost = row.get_or("cost", 0);
            if (!c.name.empty()) cards.push_back(std::move(c));
        }
        api.setClassicShopCards(cards);
    });
    lua.set_function("classic_shop_clear_cards", [&api]() {
        api.clearClassicShopCards();
    });

    // =================================================================
    // World/Unit inspection & mutation for Lua systems
    // =================================================================
    
}
