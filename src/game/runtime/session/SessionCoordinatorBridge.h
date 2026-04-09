#pragma once

#include "engine/core/ecs/Entity.h"
#include "engine/input/InputEvent.h"
#include "game/runtime/routes/RenderFlowDecisions.h"
#include "game/runtime/routes/RenderRoutes.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

class Camera3D;
class CameraSystem;
class GameStateManager;
class GameWorld;
class IRenderBackend;
class RoundSystem;
class ShopSystem;
class UnitInteractionSystem;
struct EngineServices;
struct GameConfigData;
struct GameDataDb;
struct GameServices;

namespace LogBus {
class Logger;
}

namespace engine::ecs {
class Scheduler;
class World;
}

namespace engine::log {
class Sink;
}

namespace game::runtime::render_model {
struct MeshData;
}

namespace game::runtime::session_world_backdrop {
struct Route1BackdropTuningState;
}

namespace game::runtime {
struct SharedBackendTextureCacheEntry;
namespace session_loop_runtime {
struct PauseState;
}
namespace ui_inventory_panel {
struct PanelState;
}
}

namespace game::ui {
struct UIViewport;
}

namespace game::runtime::session_coordinator_bridge {

struct Context {
    std::string snapshotPath;
    bool autoLoadSnapshotOnStartup = false;

    LogBus::Logger* log = nullptr;
    engine::log::Sink* consoleLog = nullptr;
    session_loop_runtime::PauseState* pauseState = nullptr;
    EngineServices* engineServices = nullptr;
    game::ui::UIViewport* viewport = nullptr;
    UnitInteractionSystem* unitSystem = nullptr;
    CameraSystem* cameraSystem = nullptr;
    GameStateManager* stateManager = nullptr;
    GameWorld* gameWorld = nullptr;
    GameServices* services = nullptr;
    RoundSystem* roundSystem = nullptr;
    RoundSystem** roundSystemRef = nullptr;
    ui_inventory_panel::PanelState* backendInventoryPanel = nullptr;
    std::size_t backendInventoryVisibleCount = 0u;
    bool renderWorldForInput = true;
    bool usesBackendGameUiPath = false;
    bool usesBackendGameRenderPath = false;
    std::function<void(float)> advanceTime;
    std::function<void()> hydrateBackend;
    std::function<void(float)> tickUpdateGraph;

    IRenderBackend* renderer = nullptr;
    Camera3D* camera = nullptr;
    engine::ecs::World* ecsWorld = nullptr;
    engine::ecs::Entity roundPhaseEntity{};
    const GameConfigData* config = nullptr;
    const GameDataDb* dataDb = nullptr;
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>* backendTextureByPath =
        nullptr;
    render::RenderRoutes routes{};
    bool showPerfOverlay = false;
    bool enableBackdropTiles = true;
    bool allowBackendMenuBackdrop = false;
    double simNowSec = 0.0;
    session_world_backdrop::Route1BackdropTuningState* route1BackdropTuning = nullptr;
    std::function<render_model::MeshData*(const std::string&)> ensureBackendMeshLoaded;
    std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>
        ensureBackendTextureLoaded;

    int* worldLayerPrewarmFramesRemaining = nullptr;
    int worldLayerPrewarmFrameCount = 0;
    std::function<void(unsigned int, unsigned int)> setUnitScreenSize;
    std::function<bool()> resolveRenderWorld;
    std::function<render::FrameRenderFlow(bool)> currentFrameFlow;
    std::function<void(const std::string&)> setTitle;
    std::function<void()> renderStateLayer;
    std::function<void()> resetRenderCaches;

    ShopSystem** shopSystem = nullptr;
    std::shared_ptr<UnitInteractionSystem>* unitSystemRef = nullptr;
    std::shared_ptr<CameraSystem>* cameraSystemRef = nullptr;
    std::unique_ptr<GameStateManager>* stateManagerRef = nullptr;
    std::unique_ptr<GameWorld>* gameWorldRef = nullptr;
    engine::ecs::Scheduler* scheduler = nullptr;
};

void saveDebugStateSnapshot(const Context& context);
void loadDebugStateSnapshot(const Context& context);
void maybeAutoLoadDebugStateSnapshot(const Context& context);
void handleEvent(const InputEvent& event, const Context& context);
void fixedUpdate(float dt, const Context& context);
void renderWorldLayer(const Context& context, int drawableW, int drawableH, bool renderWorld);
std::size_t prewarmWorldIndexedLayer(const Context& context,
                                     int drawableW,
                                     int drawableH,
                                     bool renderWorld);
void render(int drawableW, int drawableH, const Context& context);
void shutdown(const Context& context);

} // namespace game::runtime::session_coordinator_bridge
