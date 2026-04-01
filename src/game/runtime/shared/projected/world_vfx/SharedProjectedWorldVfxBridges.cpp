#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"

namespace game::runtime::shared_projected_scene {

void appendSharedProjectedVfxBridgesSession(
    bool useLegacyParticleVfxSnapshotBridge,
    bool useLegacyGrowlWaveVfx,
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    bool useExactTailFireCpuPath,
    bool tailFireDebugEnabled,
    GameWorld* gameWorld,
    const glm::mat4& viewProj,
    const glm::mat4& invViewProj,
    const glm::vec3& cameraWorldPos,
    int drawableW,
    int drawableH,
    float worldCellSize,
    double simNowSec,
    float lineThickness,
    std::unordered_map<int, shared_tail_fire_fallback::Anchor>& sharedTailFireAnchors,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug,
    const std::function<runtime::render_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureBackendTextureLoaded) {
    appendSharedParticleVfxSession(
        useLegacyParticleVfxSnapshotBridge,
        supportsWorldIndexedMeshes,
        hasWorldViewProj,
        useExactTailFireCpuPath,
        tailFireDebugEnabled,
        gameWorld,
        viewProj,
        invViewProj,
        cameraWorldPos,
        drawableW,
        drawableH,
        worldCellSize,
        simNowSec,
        lineThickness,
        sharedTailFireAnchors,
        backendTextureByPath,
        worldIndexedBatches,
        projectedDebug,
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
}

} // namespace game::runtime::shared_projected_scene

