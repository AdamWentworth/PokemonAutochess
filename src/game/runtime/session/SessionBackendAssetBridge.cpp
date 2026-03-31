#include "game/runtime/session/SessionBackendAssetBridge.h"

#include "engine/render/IRenderBackend.h"
#include "engine/utils/LogSink.h"
#include "game/GameWorld.h"
#include "game/config/GameDataDb.h"
#include "game/runtime/session/SessionBackendRenderHelpers.h"
#include "game/runtime/session/SessionTailFirePrewarm.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.h"
#include "game/runtime/startup/RuntimeGrowlVfxPrewarm.h"
#include "game/runtime/startup/RuntimeParticleVfxPrewarm.h"

namespace game::runtime::session_backend_asset_bridge {

session_texture_cache::TextureCache& textureCache(State& state) {
    return state.textureByPath;
}

render_model::MeshData* ensureBackendMeshLoaded(State& state,
                                                const std::string& modelPath,
                                                const engine::log::Sink& consoleLog) {
    auto& cacheEntry = state.meshByModelPath[modelPath];
    if (!cacheEntry.attemptedLoad) {
        cacheEntry.attemptedLoad = true;
        std::string err;
        if (!render_model::loadMeshFromCache(modelPath, cacheEntry.mesh, &err)) {
            cacheEntry.error = std::move(err);
            cacheEntry.mesh = {};
        }
    }

    if (!cacheEntry.error.empty()) {
        if (!cacheEntry.reportedFailure) {
            consoleLog.info(
                "[Render][ModelCache] Unable to render model '" + modelPath +
                "' (" + cacheEntry.error + ")");
            cacheEntry.reportedFailure = true;
        }
        return nullptr;
    }
    if (cacheEntry.mesh.vertices.empty() || cacheEntry.mesh.indices.empty()) {
        return nullptr;
    }
    return &cacheEntry.mesh;
}

SharedBackendTextureCacheEntry* ensureBackendTextureLoaded(State& state,
                                                           const std::string& texturePath,
                                                           bool flipVertical) {
    return session_texture_cache::ensureTextureLoaded(
        state.textureByPath,
        texturePath,
        flipVertical);
}

void hydrateBackendUnitAnimationAndScale(State& state,
                                         GameWorld* gameWorld,
                                         const GameDataDb& dataDb,
                                         bool usesBackendGameRenderPath,
                                         const engine::log::Sink& consoleLog) {
    if (!usesBackendGameRenderPath || !gameWorld) return;
    session_backend_unit_hydration::hydrateBackendUnits(
        gameWorld->getPokemons(),
        gameWorld->getBenchPokemons(),
        dataDb,
        state.animByModelPath,
        [&](const std::string& modelPath) {
            return ensureBackendMeshLoaded(state, modelPath, consoleLog);
        });
}

render_model_prewarm::ModelLoadResult loadModelForStartupPrewarm(State& state,
                                                                 const std::string& modelPath) {
    auto& cacheEntry = state.meshByModelPath[modelPath];
    if (cacheEntry.attemptedLoad) {
        return render_model_prewarm::ModelLoadResult{
            false,
            cacheEntry.error.empty() ? &cacheEntry.mesh : nullptr,
            cacheEntry.error,
        };
    }

    cacheEntry.attemptedLoad = true;
    std::string err;
    if (!render_model::loadMeshFromCache(modelPath, cacheEntry.mesh, &err)) {
        cacheEntry.error = std::move(err);
        cacheEntry.mesh = {};
        return render_model_prewarm::ModelLoadResult{
            true,
            nullptr,
            cacheEntry.error,
        };
    }

    return render_model_prewarm::ModelLoadResult{
        true,
        &cacheEntry.mesh,
        {},
    };
}

bool prewarmAnimRolesForStartupPrewarm(State& state,
                                       const std::string& modelPath,
                                       const render_model::MeshData& mesh) {
    auto it = state.animByModelPath.find(modelPath);
    const bool alreadyResolved =
        (it != state.animByModelPath.end()) && it->second.attemptedResolve;
    session_backend_unit_hydration::BackendAnimRoleEntry& roles =
        session_backend_unit_hydration::ensureBackendAnimRoles(
            modelPath,
            &mesh,
            state.animByModelPath);
    return !alreadyResolved && roles.attemptedResolve;
}

std::size_t prewarmTexturesForStartupPrewarm(IRenderBackend* renderer,
                                             const render_model::MeshData& mesh) {
    return session_backend_render_helpers::prewarmBackendWorldTexturesForMesh(renderer, &mesh);
}

std::size_t prewarmGeometryForStartupPrewarm(IRenderBackend* renderer,
                                             const render_model::MeshData& mesh) {
    if (!renderer) return 0u;
    return shared_projected_unit_backend_mesh::prewarmProjectedUnitBackendMeshGeometryCache(
        *renderer,
        mesh);
}

startup_asset_prewarm::TailFireStats prewarmTailFire(State& state, IRenderBackend* renderer) {
    return session_tail_fire_prewarm::prewarm(
        {
            .renderer = renderer,
            .backendTextureByPath = &state.textureByPath,
            .ensureBackendTextureLoaded =
                [&](const std::string& texturePath, bool flipVertical) {
                    return ensureBackendTextureLoaded(state, texturePath, flipVertical);
                },
        });
}

startup_asset_prewarm::GrowlStats prewarmGrowlVfx(State& state,
                                                  IRenderBackend* renderer,
                                                  const engine::log::Sink& consoleLog) {
    return growl_vfx_prewarm::prewarm(
        {
            .renderer = renderer,
            .backendTextureByPath = &state.textureByPath,
            .ensureBackendMeshLoaded =
                [&](const std::string& modelPath) {
                    return ensureBackendMeshLoaded(state, modelPath, consoleLog);
                },
            .ensureBackendTextureLoaded =
                [&](const std::string& texturePath, bool flipVertical) {
                    return ensureBackendTextureLoaded(state, texturePath, flipVertical);
                },
        });
}

startup_asset_prewarm::ParticleVfxStats prewarmParticleVfx(State& state,
                                                           IRenderBackend* renderer) {
    return particle_vfx_prewarm::prewarm(
        {
            .renderer = renderer,
            .ensureBackendTextureLoaded =
                [&](const std::string& texturePath, bool flipVertical) {
                    return ensureBackendTextureLoaded(state, texturePath, flipVertical);
                },
        });
}

} // namespace game::runtime::session_backend_asset_bridge
