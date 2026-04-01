#include "ScriptedState.h"

#include "game/GameServices.h"
#include "game/logging/FlowTrace.h"
#include "game/runtime/session/SessionDebugSnapshot.h"
#include <iostream>


ScriptedState::ScriptedState(GameStateManager* manager, GameWorld* world, GameServices& svc, const std::string& path)
    : stateManager(manager)
    , gameWorld(world)
    , services(svc)
    , scriptPath(path)
    , script(world, manager, svc)
{
    if (!script.loadScript(scriptPath)) {
        std::cerr << "[ScriptedState] Failed to load script: " << scriptPath << "\n";
    }
}

ScriptedState::~ScriptedState() = default;


void ScriptedState::onEnter() {
    const double tEnterStart = game::logging::flow::nowMs();
    script.onEnter();
    const double tScriptEnterEnd = game::logging::flow::nowMs();
    ensureCardUI();
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
    hasShopReadyButton = false;
    hasShopRerollButton = false;
    if (gameWorld) {
        gameWorld->clearClassicShopCards();
        gameWorld->setUnitDropZoneLayoutHint(0, false);
    }
    script.onExit();
}

void ScriptedState::update(float deltaTime) {
    // Perf/benchmark smoke can pin an auto-loaded snapshot so timed shop/menu scripts
    // do not transition away from the captured scene mid-run.
    if (game::runtime::session_debug_snapshot::pinSnapshotStateEnabled()) {
        return;
    }
    script.onUpdate(deltaTime);
}
