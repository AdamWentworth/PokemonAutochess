#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"

#include "game/runtime/shared/projected/world_vfx/SharedProjectedWorldAuthoredVfxBridge.h"

namespace game::runtime::shared_projected_scene {

void appendSharedTackleSmokeVfx(const TackleSmokeVfxArgs& args) {
    if (!args.supportsWorldIndexedMeshes || !args.hasWorldViewProj) return;
    if (!args.gameWorld || !args.worldIndexedBatches || !args.backendTextureByPath) return;
    if (!args.ensureBackendMeshLoaded || !args.ensureBackendTextureLoaded) return;
    if (args.gameWorld->countActiveTackleSmokeVfx() == 0u) return;

    TackleSmokeVFX::RenderSnapshot tackleSnapshot;
    if (!args.gameWorld->buildTackleSmokeSnapshot(tackleSnapshot)) return;
    if (tackleSnapshot.drawPasses.empty() || tackleSnapshot.rings.empty()) return;

    // Gameplay can have multiple active Tackle hits overlapping. Splitting the snapshot
    // by active ring keeps each hit on the same single-ring authored path used by VfxLab,
    // rather than collapsing several hits into one pass batch with shared sort/blend state.
    SharedAuthoredBatchVFX::RenderSnapshot singleRingSnapshot = tackleSnapshot;
    singleRingSnapshot.rings.clear();
    singleRingSnapshot.rings.reserve(1u);
    for (const auto& ring : tackleSnapshot.rings) {
        singleRingSnapshot.rings.clear();
        singleRingSnapshot.rings.push_back(ring);
        game::runtime::shared_projected_authored_vfx_bridge::appendSnapshot(
            {
                .snapshot = &singleRingSnapshot,
                .cameraWorldPos = args.cameraWorldPos,
                .backendTextureByPath = args.backendTextureByPath,
                .worldIndexedBatches = args.worldIndexedBatches,
                .ensureBackendMeshLoaded = args.ensureBackendMeshLoaded,
                .ensureBackendTextureLoaded = args.ensureBackendTextureLoaded,
                .reserveMultiplier = 4u,
            });
    }
}

void appendSharedTackleSmokeVfxSession(
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    GameWorld* gameWorld,
    const glm::vec3& cameraWorldPos,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    const std::function<runtime::render_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureBackendTextureLoaded) {
    TackleSmokeVfxArgs args{};
    args.supportsWorldIndexedMeshes = supportsWorldIndexedMeshes;
    args.hasWorldViewProj = hasWorldViewProj;
    args.gameWorld = gameWorld;
    args.cameraWorldPos = cameraWorldPos;
    args.backendTextureByPath = &backendTextureByPath;
    args.worldIndexedBatches = &worldIndexedBatches;
    args.ensureBackendMeshLoaded = ensureBackendMeshLoaded;
    args.ensureBackendTextureLoaded = ensureBackendTextureLoaded;
    appendSharedTackleSmokeVfx(args);
}

} // namespace game::runtime::shared_projected_scene
