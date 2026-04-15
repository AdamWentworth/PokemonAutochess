#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"

#include "game/runtime/shared/projected/world_vfx/SharedProjectedWorldAuthoredVfxBridge.h"

namespace game::runtime::shared_projected_scene {

void appendSharedScratchGlowVfx(const ScratchGlowVfxArgs& args) {
    if (!args.supportsWorldIndexedMeshes || !args.hasWorldViewProj) return;
    if (!args.gameWorld || !args.worldIndexedBatches || !args.backendTextureByPath) return;
    if (!args.ensureBackendMeshLoaded || !args.ensureBackendTextureLoaded) return;
    if (args.gameWorld->countActiveScratchGlowVfx() == 0u) return;

    ScratchGlowVFX::RenderSnapshot scratchSnapshot;
    if (!args.gameWorld->buildScratchGlowSnapshot(scratchSnapshot)) return;
    if (scratchSnapshot.drawPasses.empty() || scratchSnapshot.rings.empty()) return;

    game::runtime::shared_projected_authored_vfx_bridge::appendSnapshot(
        {
            .snapshot = &scratchSnapshot,
            .cameraWorldPos = args.cameraWorldPos,
            .backendTextureByPath = args.backendTextureByPath,
            .worldIndexedBatches = args.worldIndexedBatches,
            .ensureBackendMeshLoaded = args.ensureBackendMeshLoaded,
            .ensureBackendTextureLoaded = args.ensureBackendTextureLoaded,
            .reserveMultiplier = 8u,
        });
}

void appendSharedScratchGlowVfxSession(
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    GameWorld* gameWorld,
    const glm::vec3& cameraWorldPos,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    const std::function<runtime::render_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureBackendTextureLoaded) {
    ScratchGlowVfxArgs args{};
    args.supportsWorldIndexedMeshes = supportsWorldIndexedMeshes;
    args.hasWorldViewProj = hasWorldViewProj;
    args.gameWorld = gameWorld;
    args.cameraWorldPos = cameraWorldPos;
    args.backendTextureByPath = &backendTextureByPath;
    args.worldIndexedBatches = &worldIndexedBatches;
    args.ensureBackendMeshLoaded = ensureBackendMeshLoaded;
    args.ensureBackendTextureLoaded = ensureBackendTextureLoaded;
    appendSharedScratchGlowVfx(args);
}

} // namespace game::runtime::shared_projected_scene
