#pragma once

#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/startup/RuntimeRenderModelPrewarm.h"
#include "game/runtime/startup/RuntimeStartupAssetPrewarm.h"

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

namespace game::runtime::session_startup_runtime {

struct Args {
    GameContext* ctx = nullptr;
    IRenderBackend* renderer = nullptr;
    EngineServices* engineServices = nullptr;
    const ::GameDataDb* dataDb = nullptr;
    const ::GameConfigData* config = nullptr;
    GameServices* services = nullptr;
    GameWorld* gameWorld = nullptr;
    GameStateManager* stateManager = nullptr;
    LogBus::Logger* log = nullptr;
    int* worldLayerPrewarmFramesRemaining = nullptr;
    int worldLayerPrewarmFrameCount = 0;
    std::string snapshotPath;
    bool autoLoadSnapshotOnStartup = false;

    std::function<bool()> usesBackendGameRenderPath;
    std::function<render_model_prewarm::ModelLoadResult(const std::string&)> loadModel;
    std::function<bool(const std::string&, const render_model::MeshData&)> prewarmAnimRoles;
    std::function<std::size_t(const std::string&, const render_model::MeshData&)> prewarmTextures;
    std::function<std::size_t(const render_model::MeshData&)> prewarmGeometry;
    std::function<startup_asset_prewarm::TailFireStats()> prewarmTailFire;
    std::function<startup_asset_prewarm::GrowlStats()> prewarmGrowlVfx;
    std::function<startup_asset_prewarm::ParticleVfxStats()> prewarmParticleVfx;
    std::function<void(int, int)> renderWorldLayer;
};

void run(const Args& args);

} // namespace game::runtime::session_startup_runtime
