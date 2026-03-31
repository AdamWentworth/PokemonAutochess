#pragma once

#include <functional>
#include <string>

class GameStateManager;
class GameWorld;
class IRenderBackend;
struct EngineServices;
struct GameConfigData;
struct GameContext;
struct GameDataDb;
struct GameServices;

namespace LogBus {
class Logger;
}

namespace engine::log {
class Sink;
}

namespace game::runtime::session_backend_asset_bridge {
struct State;
}

namespace game::runtime::session_startup_bridge {

struct Context {
    GameContext* ctx = nullptr;
    IRenderBackend* renderer = nullptr;
    EngineServices* engineServices = nullptr;
    const ::GameDataDb* dataDb = nullptr;
    const ::GameConfigData* config = nullptr;
    GameServices* services = nullptr;
    GameWorld* gameWorld = nullptr;
    GameStateManager* stateManager = nullptr;
    LogBus::Logger* log = nullptr;
    engine::log::Sink* consoleLog = nullptr;
    session_backend_asset_bridge::State* backendAssets = nullptr;
    int* worldLayerPrewarmFramesRemaining = nullptr;
    int worldLayerPrewarmFrameCount = 0;
    std::string snapshotPath;
    bool autoLoadSnapshotOnStartup = false;
    std::function<bool()> usesBackendGameRenderPath;
    std::function<void(int, int)> renderWorldLayer;
};

void run(const Context& context);

} // namespace game::runtime::session_startup_bridge
