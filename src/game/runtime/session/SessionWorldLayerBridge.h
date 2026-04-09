#pragma once

#include "engine/core/ecs/Entity.h"
#include "game/runtime/routes/RenderRoutes.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>

class Camera3D;
class IRenderBackend;
class GameStateManager;
class GameWorld;
struct EngineServices;
struct GameServices;
struct GameConfigData;
struct GameDataDb;

namespace LogBus {
class Logger;
}

namespace engine::ecs {
class World;
}

namespace game::runtime::render_model {
struct MeshData;
}

namespace game::runtime::session_world_backdrop {
struct Route1BackdropTuningState;
}

namespace game::runtime::ui_inventory_panel {
struct PanelState;
}

namespace game::runtime::session_world_layer_bridge {

std::string currentStateScriptPath(GameStateManager* stateManager);

struct Context {
    IRenderBackend* renderer = nullptr;
    EngineServices* engineServices = nullptr;
    GameServices* services = nullptr;
    GameWorld* gameWorld = nullptr;
    Camera3D* camera = nullptr;
    engine::ecs::World* ecsWorld = nullptr;
    engine::ecs::Entity roundPhaseEntity{};
    LogBus::Logger* log = nullptr;
    ui_inventory_panel::PanelState* backendInventoryPanel = nullptr;
    std::function<void()> refreshBackendInventoryFromWorld;

    const ::GameConfigData* config = nullptr;
    const ::GameDataDb* dataDb = nullptr;
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>* backendTextureByPath =
        nullptr;

    render::RenderRoutes routes{};
    bool showPerfOverlay = false;
    bool enableBackdropTiles = true;
    bool allowBackendMenuBackdrop = false;
    double simNowSec = 0.0;
    const session_world_backdrop::Route1BackdropTuningState* route1BackdropTuning = nullptr;

    std::function<render_model::MeshData*(const std::string&)> ensureBackendMeshLoaded;
    std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>
        ensureBackendTextureLoaded;
};

std::size_t renderWorldLayer(const Context& context,
                             GameStateManager* stateManager,
                             int drawableW,
                             int drawableH,
                             bool renderWorld,
                             bool prewarmWorldIndexedOnly = false);

} // namespace game::runtime::session_world_layer_bridge
