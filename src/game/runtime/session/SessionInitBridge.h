#pragma once

#include "game/runtime/routes/RenderRoutes.h"

#include <functional>
#include <memory>
#include <string>

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
namespace ecs {
class World;
class Scheduler;
struct Entity;
}
}

namespace engine::log {
class Sink;
}

class ScriptEventBus;

namespace game {
class GameUpdateGraph;
}

namespace game::ui {
struct UIViewport;
}

namespace game::runtime::session_backend_asset_bridge {
struct State;
}

namespace game::runtime::session_init_bridge {

struct Context {
    GameContext* ctx = nullptr;
    Camera3D** camera = nullptr;
    IRenderBackend** renderer = nullptr;
    EngineServices** engineServices = nullptr;
    std::function<void(const std::string&)>* setTitleCallback = nullptr;
    render::RenderRoutes* startupRoutes = nullptr;
    bool* allowBackendMenuBackdrop = nullptr;
    bool* showPerfOverlay = nullptr;
    game::ui::UIViewport* viewport = nullptr;

    ::GameDataDb* dataDb = nullptr;
    LogBus::Logger* log = nullptr;
    engine::log::Sink* consoleLog = nullptr;
    ScriptEventBus* scriptEvents = nullptr;
    std::unique_ptr<engine::IAssetStore>* assetStore = nullptr;
    engine::XorShift32* rng = nullptr;
    engine::ManualTimeSource* timeSource = nullptr;
    ::GameConfigData* config = nullptr;
    std::unique_ptr<GameServices>* services = nullptr;
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

    session_backend_asset_bridge::State* backendAssets = nullptr;
    int* worldLayerPrewarmFramesRemaining = nullptr;
    int worldLayerPrewarmFrameCount = 0;
    std::function<bool()> usesBackendGameRenderPath;
    std::function<void(int, int, bool)> renderWorldLayer;
    std::function<void()> maybeAutoLoadSnapshot;
    std::string snapshotPath;
    bool autoLoadSnapshotOnStartup = false;
};

void run(const Context& context);

} // namespace game::runtime::session_init_bridge
