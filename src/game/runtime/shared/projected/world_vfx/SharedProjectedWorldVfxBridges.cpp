#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"

namespace game::runtime::shared_projected_scene {

void appendSharedProjectedVfxBridgesSession(
    bool useLegacyParticleVfxSnapshotBridge,
    bool useLegacyGrowlWaveVfx,
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    GameWorld* gameWorld,
    const glm::mat4& viewProj,
    const glm::mat4& invViewProj,
    const glm::vec3& cameraWorldPos,
    int drawableW,
    int drawableH,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    const std::function<runtime::render_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureBackendTextureLoaded) {
    appendSharedParticleVfxSession(
        useLegacyParticleVfxSnapshotBridge,
        supportsWorldIndexedMeshes,
        hasWorldViewProj,
        gameWorld,
        viewProj,
        invViewProj,
        cameraWorldPos,
        drawableW,
        drawableH,
        worldIndexedBatches,
        ensureBackendTextureLoaded);
    appendSharedGrowlWaveVfxSession(
        useLegacyGrowlWaveVfx,
        supportsWorldIndexedMeshes,
        hasWorldViewProj,
        gameWorld,
        cameraWorldPos,
        backendTextureByPath,
        worldIndexedBatches,
        ensureBackendMeshLoaded,
        ensureBackendTextureLoaded);
    appendSharedTackleSmokeVfxSession(
        supportsWorldIndexedMeshes,
        hasWorldViewProj,
        gameWorld,
        cameraWorldPos,
        backendTextureByPath,
        worldIndexedBatches,
        ensureBackendMeshLoaded,
        ensureBackendTextureLoaded);
    appendSharedScratchGlowVfxSession(
        supportsWorldIndexedMeshes,
        hasWorldViewProj,
        gameWorld,
        cameraWorldPos,
        backendTextureByPath,
        worldIndexedBatches,
        ensureBackendMeshLoaded,
        ensureBackendTextureLoaded);
}

} // namespace game::runtime::shared_projected_scene

