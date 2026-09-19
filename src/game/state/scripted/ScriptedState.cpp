#include "ScriptedState.h"

#include "game/GameServices.h"
#include "game/logging/FlowTrace.h"
#include "game/runtime/session/SessionDebugSnapshot.h"
#include <iostream>

ScriptedState::ScriptedState(GameStateManager *manager, GameWorld *world, GameServices &svc, const std::string &path,
                             std::string arenaScriptPath)
    : stateManager(manager), gameWorld(world), services(svc), arenaScriptPath_(std::move(arenaScriptPath)), scriptPath(path), script(world, manager, svc) {
    const double tConstructStart = game::logging::flow::nowMs();
    if (!script.loadScript(scriptPath)) {
        std::cerr << "[ScriptedState] Failed to load script: " << scriptPath << "\n";
    }
    const double tConstructEnd = game::logging::flow::nowMs();
    game::logging::flow::log(
        "scripted_state_construct",
        "script=" + scriptPath +
        " load_script=" + game::logging::flow::formatMs(tConstructEnd - tConstructStart));
}

ScriptedState::~ScriptedState() = default;


void ScriptedState::onEnter() {
    const double tEnterStart = game::logging::flow::nowMs();
    script.onEnter();
    const double tScriptEnterEnd = game::logging::flow::nowMs();
    ensureCardUI();
    resetFrontendIntro();
    const double tUiReadyEnd = game::logging::flow::nowMs();
    game::logging::flow::log(
        "scripted_state_on_enter",
        "script=" + scriptPath +
        " script_on_enter=" + game::logging::flow::formatMs(tScriptEnterEnd - tEnterStart) +
        " ensure_card_ui=" + game::logging::flow::formatMs(tUiReadyEnd - tScriptEnterEnd) +
        " total=" + game::logging::flow::formatMs(tUiReadyEnd - tEnterStart));
    if (scriptPath == "scripts/states/starter.lua") {
        game::logging::flow::noteStarterStateEntered(scriptPath);
    }
}

void ScriptedState::onExit() {
    clearBackendShopUiCache();
    uiInitialized = false;
    hasShopReadyButton = false;
    hasShopRerollButton = false;
    if (gameWorld) {
        gameWorld->clearClassicShopCards();
        gameWorld->setUnitDropZoneLayoutHint(0, false);
    }
    script.onExit();
}

void ScriptedState::update(float deltaTime) {
    // Snapshot pinning suppresses gameplay/script transitions, while this purely
    // visual sequence still advances for reproducible frontend captures.
    frontendIntro.advance(deltaTime);
    // Perf/benchmark smoke can pin an auto-loaded snapshot so timed shop/menu scripts
    // do not transition away from the captured scene mid-run.
    if (game::runtime::session_debug_snapshot::pinSnapshotStateEnabled()) {
        return;
    }
    script.onUpdate(deltaTime);
}

void ScriptedState::resetFrontendIntro() {
    game::runtime::ui_frontend::IntroConfig config;
    sol::table S = script.getScriptTable();
    sol::optional<sol::table> intro = S["frontend_intro"];
    if (cardMode == CardMode::Starter && intro) {
        config.enabled = true;
        config.holdSeconds = intro->get_or("hold_seconds", config.holdSeconds);
        config.moveSeconds = intro->get_or("move_seconds", config.moveSeconds);
        config.settleSeconds = intro->get_or("settle_seconds", config.settleSeconds);
        config.fadeSeconds = intro->get_or("fade_seconds", config.fadeSeconds);
        config.focusU = intro->get_or("focus_u", config.focusU);
        config.focusV = intro->get_or("focus_v", config.focusV);
        config.zoom = intro->get_or("zoom", config.zoom);
    }
    frontendIntro.reset(config);
    frontendCameraSequence = {};
    sol::optional<sol::table> sequence = S["frontend_backdrop_sequence"];
    if (config.enabled && sequence) {
        auto &camera = frontendCameraSequence;
        camera.atlasPrefix = sequence->get_or("atlas_prefix", std::string());
        camera.finalImage = sequence->get_or("final_image", std::string());
        camera.frameCount = sequence->get_or("frame_count", 0);
        camera.columns = sequence->get_or("columns", camera.columns);
        camera.rows = sequence->get_or("rows", camera.rows);
        camera.frameWidth = sequence->get_or("frame_width", camera.frameWidth);
        camera.frameHeight = sequence->get_or("frame_height", camera.frameHeight);
        camera.padding = sequence->get_or("padding", camera.padding);
        if (!camera.valid()) {
            camera = {};
            std::cerr << "[ScriptedState] Invalid frontend camera sequence; using its opening backdrop.\n";
        } else if (services.renderer) {
            for (int page = 0; page < camera.pageCount(); ++page)
                services.renderer->prewarmDebugSpriteTexture(camera.pagePath(page).c_str());
            services.renderer->prewarmDebugSpriteTexture(camera.finalImage.c_str());
        }
    }
}
