#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"

#include "game/runtime/shared/projected/world_vfx/SharedProjectedWorldAuthoredVfxBridge.h"

namespace game::runtime::shared_projected_scene {

void appendSharedGrowlWaveVfx(const GrowlWaveVfxArgs& args) {
    if (!args.useLegacyGrowlWaveVfx) return;
    if (!args.supportsWorldIndexedMeshes || !args.hasWorldViewProj) return;
    if (!args.gameWorld || !args.worldIndexedBatches || !args.backendTextureByPath) return;
    if (!args.ensureBackendMeshLoaded || !args.ensureBackendTextureLoaded) return;
    if (args.gameWorld->countActiveGrowlWaveVfx() == 0u) return;

    GrowlWaveVFX::RenderSnapshot growlSnapshot;
    if (!args.gameWorld->buildGrowlWaveSnapshot(growlSnapshot)) return;
    if (growlSnapshot.drawPasses.empty() || growlSnapshot.rings.empty()) return;

    game::runtime::shared_projected_authored_vfx_bridge::appendSnapshot(
        {
            .snapshot = &growlSnapshot,
            .cameraWorldPos = args.cameraWorldPos,
            .backendTextureByPath = args.backendTextureByPath,
            .worldIndexedBatches = args.worldIndexedBatches,
            .ensureBackendMeshLoaded = args.ensureBackendMeshLoaded,
            .ensureBackendTextureLoaded = args.ensureBackendTextureLoaded,
            .reserveMultiplier = 4u,
        });
}

void appendSharedGrowlWaveVfxSession(
    bool useLegacyGrowlWaveVfx,
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    GameWorld* gameWorld,
    const glm::vec3& cameraWorldPos,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    const std::function<runtime::render_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureBackendTextureLoaded) {
    GrowlWaveVfxArgs args{};
    args.useLegacyGrowlWaveVfx = useLegacyGrowlWaveVfx;
    args.supportsWorldIndexedMeshes = supportsWorldIndexedMeshes;
    args.hasWorldViewProj = hasWorldViewProj;
    args.gameWorld = gameWorld;
    args.cameraWorldPos = cameraWorldPos;
    args.backendTextureByPath = &backendTextureByPath;
    args.worldIndexedBatches = &worldIndexedBatches;
    args.ensureBackendMeshLoaded = ensureBackendMeshLoaded;
    args.ensureBackendTextureLoaded = ensureBackendTextureLoaded;
    appendSharedGrowlWaveVfx(args);
}

} // namespace game::runtime::shared_projected_scene

