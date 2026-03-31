#include <memory>
#include <sstream>
#include <string>

#include "engine/core/ecs/ISystem.h"
#include "engine/core/ecs/Scheduler.h"
#include "engine/utils/LogSink.h"
#include "game/GameStateManager.h"
#include "game/GameWorld.h"
#include "game/logging/LogBus.h"
#include "game/runtime/session/SessionLifecycleBridge.h"

namespace {

struct DummySystem final : engine::ecs::ISystem {
    void update(engine::ecs::World&, float) override {}
};

} // namespace

bool test_session_lifecycle_bridge_contract(std::string& outFail) {
    std::ostringstream info;
    std::ostringstream err;
    engine::log::Sink sink("TEST", &info, &err);
    LogBus::Logger log;
    engine::ecs::Scheduler scheduler;
    scheduler.add(std::make_unique<DummySystem>());
    ShopSystem* shopSystem = reinterpret_cast<ShopSystem*>(1);
    RoundSystem* roundSystem = reinterpret_cast<RoundSystem*>(1);
    std::shared_ptr<UnitInteractionSystem> unitSystem;
    std::shared_ptr<CameraSystem> cameraSystem;
    std::unique_ptr<GameStateManager> stateManager;
    std::unique_ptr<GameWorld> gameWorld;

    game::runtime::session_lifecycle_bridge::shutdown(
        {
            .consoleLog = &sink,
            .log = &log,
            .shopSystem = &shopSystem,
            .roundSystem = &roundSystem,
            .unitSystem = &unitSystem,
            .cameraSystem = &cameraSystem,
            .stateManager = &stateManager,
            .gameWorld = &gameWorld,
            .scheduler = &scheduler,
        });

    if (shopSystem != nullptr || roundSystem != nullptr || scheduler.size() != 0u) {
        outFail =
            "SessionLifecycleBridge should clear raw session system pointers and empty the scheduler during shutdown.";
        return false;
    }

    if (info.str().find("[Shutdown] Game.") == std::string::npos ||
        info.str().find("[Shutdown] Game done.") == std::string::npos) {
        outFail =
            "SessionLifecycleBridge should emit the standard session shutdown log lines.";
        return false;
    }

    return true;
}
