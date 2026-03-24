#include <string>
#include <sol/sol.hpp>

#include "engine/core/Paths.h"

static bool hasEntry(sol::table entries, const char* id) {
    const std::string want = id ? id : "";
    for (auto&& kv : entries) {
        const sol::object value = kv.second;
        if (!value.is<sol::table>()) continue;
        const sol::table entry = value.as<sol::table>();
        if (entry["id"].get_or(std::string()) == want) {
            return true;
        }
    }
    return false;
}

static std::string entryLabel(sol::table entries, const char* id) {
    const std::string want = id ? id : "";
    for (auto&& kv : entries) {
        const sol::object value = kv.second;
        if (!value.is<sol::table>()) continue;
        const sol::table entry = value.as<sol::table>();
        if (entry["id"].get_or(std::string()) == want) {
            return entry["label"].get_or(std::string());
        }
    }
    return {};
}

bool test_mode_split_flow(std::string& outFail) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);

    const std::string flowPath = engine::paths::data("scripts/states/flow.lua");
    sol::load_result chunk = lua.load_file(flowPath);
    if (!chunk.valid()) {
        sol::error e = chunk;
        outFail = std::string("Failed to load flow.lua: ") + e.what();
        return false;
    }
    sol::protected_function_result r = chunk();
    if (!r.valid()) {
        sol::error e = r;
        outFail = std::string("Failed to execute flow.lua: ") + e.what();
        return false;
    }

    sol::function routeFn = lua["next_route_after_placement"];
    if (!routeFn.valid()) {
        outFail = "next_route_after_placement is missing.";
        return false;
    }

    lua.set_function("get_game_mode", []() { return std::string("classic"); });
    {
        sol::protected_function_result rr = routeFn("bulbasaur");
        if (!rr.valid()) {
            sol::error e = rr;
            outFail = std::string("classic route call failed: ") + e.what();
            return false;
        }
        const std::string route = rr.get<std::string>();
        if (route != "scripts/states/route1.lua") {
            outFail = "classic mode did not route to shared route1 state path.";
            return false;
        }
    }

    lua.set_function("get_game_mode", []() { return std::string("adventure"); });
    {
        sol::protected_function_result rr = routeFn("bulbasaur");
        if (!rr.valid()) {
            sol::error e = rr;
            outFail = std::string("adventure route call failed: ") + e.what();
            return false;
        }
        const std::string route = rr.get<std::string>();
        if (route != "scripts/states/route1.lua") {
            outFail = "adventure mode did not route to shared route1 state path.";
            return false;
        }
    }

    return true;
}

bool test_mode_split_menu_entries(std::string& outFail) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);

    bool started = false;
    std::string requestedNewGameMode;
    std::string restartMenuScreen;
    std::string requestedRendererBackend;
    int restartRequests = 0;

    lua.set_function("get_game_mode", []() { return std::string("classic"); });
    lua.set_function("get_has_started_game", [&started]() { return started; });
    lua.set_function("get_video_mode", [](sol::this_state s) {
        sol::state_view L(s);
        sol::table t = L.create_table();
        t["width"] = 1280;
        t["height"] = 720;
        t["fullscreen"] = false;
        return t;
    });
    lua.set_function("start_new_game", [&requestedNewGameMode](const std::string& mode) {
        requestedNewGameMode = mode;
    });
    lua.set_function("set_game_mode", [](const std::string&) {});
    lua.set_function("emit", [](const std::string&, const std::string&) {});
    lua.set_function("set_video_mode", [](int, int, bool) { return true; });
    lua.set_function("get_renderer_backend_pref", []() { return std::string("auto"); });
    lua.set_function("is_renderer_backend_implemented", [](const std::string& backend) {
        return backend == "opengl" || backend == "d3d12";
    });
    lua.set_function("set_renderer_backend_pref", [&requestedRendererBackend](const std::string& backend) {
        requestedRendererBackend = backend;
        return true;
    });
    lua.set_function("get_require_discrete_gpu_pref", []() { return false; });
    lua.set_function("set_require_discrete_gpu_pref", [](bool) { return true; });
    lua.set_function("get_active_renderer_backend", []() { return std::string("opengl"); });
    lua.set_function("get_active_gpu_renderer", []() { return std::string("Mock GPU"); });
    lua.set_function("get_gpu_adapters", [](sol::this_state s) {
        sol::state_view L(s);
        sol::table t = L.create_table();
        t[1] = std::string("Mock GPU");
        return t;
    });
    lua.set_function("get_preferred_gpu_adapter_pref", []() { return std::string(); });
    lua.set_function("set_preferred_gpu_adapter_pref", [](const std::string&) { return true; });
    lua.set_function("is_active_gpu_discrete", []() { return true; });
    lua.set_function("request_restart_to_menu", [&restartMenuScreen, &restartRequests](const std::string& screen) {
        restartMenuScreen = screen;
        ++restartRequests;
        return true;
    });
    lua.set_function("consume_boot_menu_screen", []() { return std::string(); });
    lua.set_function("request_quit", []() {});
    lua.set_function("set_has_started_game", [](bool) {});
    lua.set_function("push_state", [](const std::string&) {});
    lua.set_function("pop_state", []() {});

    const std::string menuPath = engine::paths::data("scripts/states/main_menu.lua");
    sol::load_result chunk = lua.load_file(menuPath);
    if (!chunk.valid()) {
        sol::error e = chunk;
        outFail = std::string("Failed to load main_menu.lua: ") + e.what();
        return false;
    }
    sol::protected_function_result r = chunk();
    if (!r.valid()) {
        sol::error e = r;
        outFail = std::string("Failed to execute main_menu.lua: ") + e.what();
        return false;
    }

    sol::function onEnter = lua["on_enter"];
    sol::function getEntries = lua["get_text_menu_entries"];
    sol::function onClick = lua["on_text_menu_click"];
    sol::function onBack = lua["on_text_menu_back"];
    if (!onEnter.valid() || !getEntries.valid() || !onClick.valid() || !onBack.valid()) {
        outFail = "main_menu.lua did not expose expected menu functions.";
        return false;
    }

    {
        started = false;
        requestedNewGameMode.clear();
        restartMenuScreen.clear();
        restartRequests = 0;
        sol::protected_function_result e1 = onEnter();
        if (!e1.valid()) {
            sol::error e = e1;
            outFail = std::string("main_menu on_enter (not-started) failed: ") + e.what();
            return false;
        }
        sol::protected_function_result listRes = getEntries();
        if (!listRes.valid()) {
            sol::error e = listRes;
            outFail = std::string("main_menu entries (not-started) failed: ") + e.what();
            return false;
        }
        const sol::table entries = listRes.get<sol::table>();
        if (!hasEntry(entries, "mode_classic") || !hasEntry(entries, "mode_adventure")) {
            outFail = "main menu should show mode selectors before first start.";
            return false;
        }
        if (hasEntry(entries, "new_game_classic") || hasEntry(entries, "new_game_adventure")) {
            outFail = "main menu should not show new-game mode shortcuts before first start.";
            return false;
        }

        onClick("open_settings");
        onClick("open_video");
        sol::protected_function_result videoRes = getEntries();
        if (!videoRes.valid()) {
            sol::error e = videoRes;
            outFail = std::string("main_menu video entries (not-started) failed: ") + e.what();
            return false;
        }
        const sol::table videoEntries = videoRes.get<sol::table>();
        if (!hasEntry(videoEntries, "video_vsync") || hasEntry(videoEntries, "advanced_apply_restart")) {
            outFail = "display menu should expose display settings and keep restart-required advanced settings elsewhere.";
            return false;
        }
        onClick("settings_back");
        onClick("open_advanced");
        sol::protected_function_result advancedRes = getEntries();
        if (!advancedRes.valid()) {
            sol::error e = advancedRes;
            outFail = std::string("main_menu advanced entries (not-started) failed: ") + e.what();
            return false;
        }
        const sol::table advancedEntries = advancedRes.get<sol::table>();
        if (!hasEntry(advancedEntries, "advanced_apply_restart")) {
            outFail = "advanced menu should expose Apply + Restart entry.";
            return false;
        }
        const std::string rendererLabel = entryLabel(advancedEntries, "advanced_renderer_backend");
        if (rendererLabel.find("Vulkan") != std::string::npos) {
            outFail = "advanced menu should not expose unimplemented Vulkan backend.";
            return false;
        }
        requestedRendererBackend.clear();
        onClick("advanced_renderer_backend");
        if (requestedRendererBackend != "d3d12") {
            outFail = "advanced backend cycling should skip unimplemented Vulkan and move to d3d12.";
            return false;
        }
        onClick("advanced_apply_restart");
        if (restartRequests != 1 || restartMenuScreen != "advanced") {
            outFail = "advanced_apply_restart should request restart to advanced screen when no run is active.";
            return false;
        }
        onClick("open_settings");
        onClick("open_video");
        onBack();
        sol::protected_function_result backToSettingsRes = getEntries();
        if (!backToSettingsRes.valid()) {
            sol::error e = backToSettingsRes;
            outFail = std::string("on_text_menu_back from video failed: ") + e.what();
            return false;
        }
        const sol::table settingsEntries = backToSettingsRes.get<sol::table>();
        if (!hasEntry(settingsEntries, "open_video")) {
            outFail = "on_text_menu_back should return from video to settings screen.";
            return false;
        }
        onBack();
        sol::protected_function_result backToMainRes = getEntries();
        if (!backToMainRes.valid()) {
            sol::error e = backToMainRes;
            outFail = std::string("on_text_menu_back from settings failed: ") + e.what();
            return false;
        }
        const sol::table mainEntries = backToMainRes.get<sol::table>();
        if (!hasEntry(mainEntries, "open_settings")) {
            outFail = "on_text_menu_back should return from settings to main screen.";
            return false;
        }
    }

    {
        started = true;
        requestedNewGameMode.clear();
        restartMenuScreen.clear();
        restartRequests = 0;
        sol::protected_function_result e2 = onEnter();
        if (!e2.valid()) {
            sol::error e = e2;
            outFail = std::string("main_menu on_enter (started) failed: ") + e.what();
            return false;
        }
        sol::protected_function_result listRes = getEntries();
        if (!listRes.valid()) {
            sol::error e = listRes;
            outFail = std::string("main_menu entries (started) failed: ") + e.what();
            return false;
        }
        const sol::table entries = listRes.get<sol::table>();
        if (!hasEntry(entries, "new_game_classic") || !hasEntry(entries, "new_game_adventure")) {
            outFail = "main menu should show New Classic/New Adventure once a run has started.";
            return false;
        }
        if (hasEntry(entries, "mode_classic") || hasEntry(entries, "mode_adventure")) {
            outFail = "main menu should hide direct mode toggle once a run has started.";
            return false;
        }

        sol::protected_function_result clickRes = onClick("new_game_adventure");
        if (!clickRes.valid()) {
            sol::error e = clickRes;
            outFail = std::string("clicking new_game_adventure failed: ") + e.what();
            return false;
        }
        if (requestedNewGameMode != "adventure") {
            outFail = "new_game_adventure did not call start_new_game(\"adventure\").";
            return false;
        }

        onClick("open_settings");
        onClick("open_advanced");
        onClick("advanced_apply_restart");
        if (restartRequests != 0) {
            outFail = "advanced_apply_restart should show confirmation before restart during active run.";
            return false;
        }
        sol::protected_function_result confirmRes = getEntries();
        if (!confirmRes.valid()) {
            sol::error e = confirmRes;
            outFail = std::string("main_menu restart confirm entries failed: ") + e.what();
            return false;
        }
        const sol::table confirmEntries = confirmRes.get<sol::table>();
        if (!hasEntry(confirmEntries, "advanced_restart_confirm_yes") ||
            !hasEntry(confirmEntries, "advanced_restart_confirm_no")) {
            outFail = "advanced restart confirm screen entries missing.";
            return false;
        }
        onClick("advanced_restart_confirm_yes");
        if (restartRequests != 1 || restartMenuScreen != "advanced") {
            outFail = "advanced restart confirmation should request restart to advanced screen.";
            return false;
        }
    }

    return true;
}
