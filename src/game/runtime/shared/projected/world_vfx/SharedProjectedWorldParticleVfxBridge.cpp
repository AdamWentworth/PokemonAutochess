#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"

#include "game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.h"
#include "game/runtime/shared/vfx/particles/SharedParticleVfxBridgeDispatch.h"

namespace game::runtime::shared_projected_scene {

void appendSharedParticleVfx(const ParticleVfxArgs& args) {
    if (!args.useLegacyParticleVfxSnapshotBridge) return;
    if (!args.supportsWorldIndexedMeshes || !args.hasWorldViewProj) return;
    if (!args.gameWorld || !args.worldIndexedBatches) {
        return;
    }
    if (!args.ensureBackendTextureLoaded) return;

    if (args.gameWorld->countActiveParticleVfx() > 0u) {
        GameWorld::ParticleVfxSnapshots vfxSnapshots;
        (void)args.gameWorld->buildParticleVfxSnapshots(vfxSnapshots);

        const auto appendSnapshotAsBillboards =
            [&](const char* label, const ParticleSystem::RenderSnapshot& snapshot) -> bool {
            return game::runtime::shared_particle_snapshot_billboards::appendSnapshotAsBillboards(
                label,
                snapshot,
                args.viewProj,
                args.invViewProj,
                args.cameraWorldPos,
                args.drawableW,
                args.drawableH,
                args.ensureBackendTextureLoaded,
                *args.worldIndexedBatches);
        };
        (void)game::runtime::shared_particle_bridge_dispatch::appendStandardSnapshots(
            vfxSnapshots,
            appendSnapshotAsBillboards);
    }
}

void appendSharedParticleVfxSession(
    bool useLegacyParticleVfxSnapshotBridge,
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    GameWorld* gameWorld,
    const glm::mat4& viewProj,
    const glm::mat4& invViewProj,
    const glm::vec3& cameraWorldPos,
    int drawableW,
    int drawableH,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureBackendTextureLoaded) {
    ParticleVfxArgs args{};
    args.useLegacyParticleVfxSnapshotBridge = useLegacyParticleVfxSnapshotBridge;
    args.supportsWorldIndexedMeshes = supportsWorldIndexedMeshes;
    args.hasWorldViewProj = hasWorldViewProj;
    args.gameWorld = gameWorld;
    args.viewProj = viewProj;
    args.invViewProj = invViewProj;
    args.cameraWorldPos = cameraWorldPos;
    args.drawableW = drawableW;
    args.drawableH = drawableH;
    args.worldIndexedBatches = &worldIndexedBatches;
    args.ensureBackendTextureLoaded = ensureBackendTextureLoaded;
    appendSharedParticleVfx(args);
}

} // namespace game::runtime::shared_projected_scene

