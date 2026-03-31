#include "game/runtime/session/SessionLifecycleBridge.h"

#include "engine/core/ecs/Scheduler.h"
#include "engine/utils/LogSink.h"
#include "game/GameStateManager.h"
#include "game/GameWorld.h"
#include "game/logging/LogBus.h"
#include "game/systems/CameraSystem.h"
#include "game/systems/UnitInteractionSystem.h"

namespace game::runtime::session_lifecycle_bridge {

void shutdown(const Context& context) {
    if (context.consoleLog) {
        context.consoleLog->info("[Shutdown] Game.");
    }

    if (context.log) {
        context.log->attach(nullptr);
        context.log->attachCatchFeed(nullptr);
        context.log->attachEconomyFeed(nullptr);
    }
    if (context.shopSystem) {
        *context.shopSystem = nullptr;
    }
    if (context.roundSystem) {
        *context.roundSystem = nullptr;
    }
    if (context.unitSystem) {
        context.unitSystem->reset();
    }
    if (context.cameraSystem) {
        context.cameraSystem->reset();
    }
    if (context.stateManager) {
        context.stateManager->reset();
    }
    if (context.gameWorld) {
        context.gameWorld->reset();
    }
    if (context.scheduler) {
        context.scheduler->clear();
    }

    if (context.consoleLog) {
        context.consoleLog->info("[Shutdown] Game done.");
    }
}

} // namespace game::runtime::session_lifecycle_bridge
