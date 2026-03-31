#pragma once

#include "game/runtime/session/SessionBackendUnitHydration.h"
#include "game/runtime/session/SessionTextureCache.h"
#include "game/runtime/startup/RuntimeRenderModelPrewarm.h"
#include "game/runtime/startup/RuntimeStartupAssetPrewarm.h"

#include <string>
#include <unordered_map>

class GameWorld;
class IRenderBackend;
struct GameDataDb;

namespace engine::log {
class Sink;
}

namespace game::runtime::render_model {
struct MeshData;
}

namespace game::runtime::session_backend_asset_bridge {

struct BackendMeshCacheEntry {
    bool attemptedLoad = false;
    bool reportedFailure = false;
    render_model::MeshData mesh;
    std::string error;
};

struct State {
    std::unordered_map<std::string, BackendMeshCacheEntry> meshByModelPath;
    session_backend_unit_hydration::BackendAnimRoleCache animByModelPath;
    session_texture_cache::TextureCache textureByPath;
};

session_texture_cache::TextureCache& textureCache(State& state);

render_model::MeshData* ensureBackendMeshLoaded(State& state,
                                                const std::string& modelPath,
                                                const engine::log::Sink& consoleLog);

SharedBackendTextureCacheEntry* ensureBackendTextureLoaded(State& state,
                                                           const std::string& texturePath,
                                                           bool flipVertical = false);

void hydrateBackendUnitAnimationAndScale(State& state,
                                         GameWorld* gameWorld,
                                         const GameDataDb& dataDb,
                                         bool usesBackendGameRenderPath,
                                         const engine::log::Sink& consoleLog);

render_model_prewarm::ModelLoadResult loadModelForStartupPrewarm(State& state,
                                                                 const std::string& modelPath);

bool prewarmAnimRolesForStartupPrewarm(State& state,
                                       const std::string& modelPath,
                                       const render_model::MeshData& mesh);

std::size_t prewarmTexturesForStartupPrewarm(IRenderBackend* renderer,
                                             const render_model::MeshData& mesh);

std::size_t prewarmGeometryForStartupPrewarm(IRenderBackend* renderer,
                                             const render_model::MeshData& mesh);

startup_asset_prewarm::TailFireStats prewarmTailFire(State& state, IRenderBackend* renderer);

startup_asset_prewarm::GrowlStats prewarmGrowlVfx(State& state,
                                                  IRenderBackend* renderer,
                                                  const engine::log::Sink& consoleLog);

startup_asset_prewarm::ParticleVfxStats prewarmParticleVfx(State& state,
                                                           IRenderBackend* renderer);

} // namespace game::runtime::session_backend_asset_bridge
