#include "game/runtime/shared/vfx/tail_fire/SharedTailFireRenderContext.h"

#include "game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.h"

namespace game::runtime::shared_tail_fire_render {

bool appendSnapshotBillboards(
    const char* label,
    const ParticleSystem::RenderSnapshot& snapshot,
    const glm::mat4& viewProj,
    const glm::mat4& invViewProj,
    const glm::vec3& cameraWorldPos,
    int drawableW,
    int drawableH,
    const RenderContext& context) {
    if (!context.anchors ||
        !context.backendTextureByPath ||
        !context.worldIndexedBatches ||
        !context.ensureBackendTextureLoaded) {
        return false;
    }

    return shared_particle_snapshot_billboards::appendSnapshotAsBillboards(
        label,
        snapshot,
        viewProj,
        invViewProj,
        cameraWorldPos,
        drawableW,
        drawableH,
        *context.backendTextureByPath,
        context.ensureBackendTextureLoaded,
        context.anchors,
        context.useExactTailFireCpuPath,
        context.tailFireDebugEnabled,
        *context.worldIndexedBatches);
}

} // namespace game::runtime::shared_tail_fire_render
