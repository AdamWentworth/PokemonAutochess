#pragma once

#include <memory>

namespace engine::log {
class Sink;
}

namespace LogBus {
class Logger;
}

class CameraSystem;
class GameStateManager;
class GameWorld;
class RoundSystem;
class ShopSystem;
class UnitInteractionSystem;

namespace engine::ecs {
class Scheduler;
}

namespace game::runtime::session_lifecycle_bridge {

struct Context {
    engine::log::Sink* consoleLog = nullptr;
    LogBus::Logger* log = nullptr;
    ShopSystem** shopSystem = nullptr;
    RoundSystem** roundSystem = nullptr;
    std::shared_ptr<UnitInteractionSystem>* unitSystem = nullptr;
    std::shared_ptr<CameraSystem>* cameraSystem = nullptr;
    std::unique_ptr<GameStateManager>* stateManager = nullptr;
    std::unique_ptr<GameWorld>* gameWorld = nullptr;
    engine::ecs::Scheduler* scheduler = nullptr;
};

void shutdown(const Context& context);

} // namespace game::runtime::session_lifecycle_bridge
