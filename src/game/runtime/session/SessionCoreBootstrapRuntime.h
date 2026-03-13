#pragma once

#include "engine/core/ecs/Entity.h"
#include "game/runtime/routes/RenderRoutes.h"

#include <memory>

class Camera3D;
class IRenderBackend;
class GameStateManager;
class GameWorld;
class ShopSystem;
class RoundSystem;
class CameraSystem;
class UnitInteractionSystem;
struct EngineServices;
struct GameConfigData;
struct GameContext;
struct GameDataDb;
struct GameServices;

namespace LogBus {
class Logger;
}

namespace engine {
class IAssetStore;
class XorShift32;
class ManualTimeSource;
struct CoreServices;
}

namespace engine::ecs {
class World;
class Scheduler;
}

class ScriptEventBus;

namespace game {
class GameUpdateGraph;
}

namespace game::ui {
struct UIViewport;
}

namespace game::runtime::session_core_bootstrap_runtime {

struct Args {
    GameContext* ctx = nullptr;
    Camera3D* camera = nullptr;
    IRenderBackend* renderer = nullptr;
    EngineServices* engineServices = nullptr;
    const render::RenderRoutes* startupRoutes = nullptr;
    ::GameDataDb* dataDb = nullptr;
    LogBus::Logger* log = nullptr;
    ScriptEventBus* scriptEvents = nullptr;
    std::unique_ptr<engine::IAssetStore>* assetStore = nullptr;
    engine::XorShift32* rng = nullptr;
    engine::ManualTimeSource* timeSource = nullptr;
    ::GameConfigData* config = nullptr;
    std::unique_ptr<GameServices>* services = nullptr;
    game::ui::UIViewport* viewport = nullptr;
    engine::CoreServices* coreServices = nullptr;
    engine::ecs::World* ecsWorld = nullptr;
    engine::ecs::Entity* roundPhaseEntity = nullptr;
    std::unique_ptr<GameStateManager>* stateManager = nullptr;
    std::unique_ptr<GameWorld>* gameWorld = nullptr;
    engine::ecs::Scheduler* scheduler = nullptr;
    game::GameUpdateGraph* updateGraph = nullptr;
    std::shared_ptr<CameraSystem>* cameraSystem = nullptr;
    std::shared_ptr<UnitInteractionSystem>* unitSystem = nullptr;
    ShopSystem** shopSystem = nullptr;
    RoundSystem** roundSystem = nullptr;
};

void run(const Args& args);

} // namespace game::runtime::session_core_bootstrap_runtime
