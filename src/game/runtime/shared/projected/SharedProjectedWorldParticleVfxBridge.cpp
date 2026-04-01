#include "game/runtime/shared/projected/SharedProjectedWorldSceneHelpers.h"
#include "game/runtime/shared/projected/SharedProjectedWorldTailFireVfxBridge.h"

#include "game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.h"
#include "game/runtime/shared/vfx/particles/SharedParticleVfxBridgeDispatch.h"

#include <cstring>

namespace game::runtime::shared_projected_scene {

void appendSharedParticleVfx(const ParticleVfxArgs& args) {
    if (!args.useLegacyParticleVfxSnapshotBridge) return;
    if (!args.supportsWorldIndexedMeshes || !args.hasWorldViewProj) return;
    if (!args.gameWorld || !args.projectedDebug || !args.sharedTailFireAnchors ||
        !args.backendTextureByPath || !args.worldIndexedBatches) {
        return;
    }
    if (!args.ensureBackendTextureLoaded) return;

    bool appendedTailFireBillboards = false;
    bool appendedLeechDrainBillboards = false;
    const TailFireVFXConfig& tailFireFallbackCfg = getPrimaryTailFireConfig();
    const auto tailFireRender = tail_fire_vfx_bridge::makeRenderContext(args);
    const bool wantsAnchoredSingleFlipbook =
        tail_fire_vfx_bridge::wantsAnchoredSingleFlipbook(tailFireFallbackCfg);
    if (wantsAnchoredSingleFlipbook) {
        appendedTailFireBillboards =
            tail_fire_vfx_bridge::appendAnchoredSingleFlipbook(
                args,
                tailFireFallbackCfg,
                tailFireRender);
    }

    if (args.gameWorld->countActiveParticleVfx() > 0u) {
        GameWorld::ParticleVfxSnapshots vfxSnapshots;
        (void)args.gameWorld->buildParticleVfxSnapshots(vfxSnapshots);

        const auto appendSnapshotAsBillboards =
            [&](const char* label, const ParticleSystem::RenderSnapshot& snapshot) -> bool {
            if (wantsAnchoredSingleFlipbook &&
                appendedTailFireBillboards &&
                label != nullptr &&
                std::strcmp(label, "tail_fire") == 0) {
                return true;
            }
            return game::runtime::shared_tail_fire_render::appendSnapshotBillboards(
                label,
                snapshot,
                args.viewProj,
                args.invViewProj,
                args.cameraWorldPos,
                args.drawableW,
                args.drawableH,
                tailFireRender);
        };
        const auto particleDispatchResult =
            game::runtime::shared_particle_bridge_dispatch::appendStandardSnapshots(
                vfxSnapshots,
                appendSnapshotAsBillboards);
        appendedTailFireBillboards = particleDispatchResult.appendedTailFireBillboards;
        appendedLeechDrainBillboards = particleDispatchResult.appendedLeechDrainBillboards;
    }

    appendedTailFireBillboards = tail_fire_vfx_bridge::appendSyntheticTailFireBillboards(
        args,
        tailFireFallbackCfg,
        tailFireRender,
        appendedTailFireBillboards);
    tail_fire_vfx_bridge::appendProjectedTailFireFallbackOverlay(
        args,
        appendedTailFireBillboards);
}

void appendSharedParticleVfxSession(
    bool useLegacyParticleVfxSnapshotBridge,
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
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureBackendTextureLoaded) {
    ParticleVfxArgs args{};
    args.useLegacyParticleVfxSnapshotBridge = useLegacyParticleVfxSnapshotBridge;
    args.supportsWorldIndexedMeshes = supportsWorldIndexedMeshes;
    args.hasWorldViewProj = hasWorldViewProj;
    args.useExactTailFireCpuPath = useExactTailFireCpuPath;
    args.tailFireDebugEnabled = tailFireDebugEnabled;
    args.gameWorld = gameWorld;
    args.viewProj = viewProj;
    args.invViewProj = invViewProj;
    args.cameraWorldPos = cameraWorldPos;
    args.drawableW = drawableW;
    args.drawableH = drawableH;
    args.worldCellSize = worldCellSize;
    args.simNowSec = simNowSec;
    args.lineThickness = lineThickness;
    args.sharedTailFireAnchors = &sharedTailFireAnchors;
    args.backendTextureByPath = &backendTextureByPath;
    args.worldIndexedBatches = &worldIndexedBatches;
    args.projectedDebug = &projectedDebug;
    args.ensureBackendTextureLoaded = ensureBackendTextureLoaded;
    appendSharedParticleVfx(args);
}

} // namespace game::runtime::shared_projected_scene
