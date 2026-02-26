#include "ScriptedState.h"

#include "game/GameServices.h"
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
    script.onEnter();
    ensureCardUI();
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
    script.onUpdate(deltaTime);
}
