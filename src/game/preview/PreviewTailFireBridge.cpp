#include "game/preview/PreviewTailFireBridge.h"

#include <glm/gtc/matrix_inverse.hpp>

#include "game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireFallbackEmitter.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFirePlaybackPolicy.h"

namespace game::preview {

void renderPreviewTailFire(const PreviewTailFireBridgeArgs& args) {
    if (!args.camera ||
        !args.visual ||
        !args.visual->valid ||
        !args.backendTextureByPath ||
        !args.modelScratch ||
        !args.tailFireScratch ||
        !args.appendModelBatches ||
        !args.submitScratch) {
        return;
    }

    (void)args.appendModelBatches(
        *args.camera,
        args.surfaceWidth,
        args.surfaceHeight,
        *args.visual,
        args.worldPos,
        args.yawDeg,
        args.side);

    if (game::runtime::shared_tail_fire_playback_policy::hasAuthoredFireMeshBatches(
            args.modelScratch->worldIndexedBatches)) {
        auto& scratch = *args.tailFireScratch;
        game::runtime::session_render_scratch::ensureCapacity(scratch);
        game::runtime::session_render_scratch::beginFrame(scratch, true, args.renderer);
        scratch.worldIndexedBatches.reserve(args.modelScratch->worldIndexedBatches.size());
        for (const auto& batch : args.modelScratch->worldIndexedBatches) {
            if (!game::runtime::shared_tail_fire_playback_policy::batchUsesAuthoredFireMesh(batch)) {
                continue;
            }
            scratch.worldIndexedBatches.push_back(batch);
        }
        args.submitScratch(
            *args.camera,
            args.surfaceWidth,
            args.surfaceHeight,
            scratch,
            false,
            false);
        return;
    }

    if (!args.fallbackConfig ||
        !game::runtime::shared_tail_fire_playback_policy::shouldRenderSyntheticTailFireFallback(
            args.visual->speciesName,
            args.modelScratch->worldIndexedBatches)) {
        return;
    }

    auto& scratch = *args.tailFireScratch;
    game::runtime::session_render_scratch::ensureCapacity(scratch);
    game::runtime::session_render_scratch::beginFrame(scratch, true, args.renderer);

    std::vector<PokemonInstance> boardUnits;
    boardUnits.reserve(1u);
    boardUnits.push_back(makePreviewRuntimeUnit(
        *args.visual,
        args.worldPos,
        args.yawDeg,
        args.side));
    static const std::vector<PokemonInstance> kNoBenchUnits;

    const glm::mat4 viewProj =
        args.camera->getProjectionMatrix() * args.camera->getViewMatrix();
    const glm::mat4 invViewProj = glm::inverse(viewProj);
    (void)game::runtime::shared_tail_fire_fallback::appendSyntheticTailFire(
        {
            .worldCellSize = args.worldCellSize,
            .simNowSec = args.simNowSec,
            .cfg = args.fallbackConfig,
            .anchors = &args.modelScratch->sharedTailFireAnchors,
            .pokemons = &boardUnits,
            .benchPokemons = &kNoBenchUnits,
            .appendSnapshot =
                [&](const char* label, const ParticleSystem::RenderSnapshot& snapshot) -> bool {
                    return game::runtime::shared_particle_snapshot_billboards::appendSnapshotAsBillboards(
                        label,
                        snapshot,
                        viewProj,
                        invViewProj,
                        args.camera->getPosition(),
                        args.surfaceWidth,
                        args.surfaceHeight,
                        *args.backendTextureByPath,
                        [&](const std::string& texturePath, bool flipVertical)
                            -> game::runtime::SharedBackendTextureCacheEntry* {
                            if (!args.ensureBackendTextureLoaded) {
                                return nullptr;
                            }
                            return args.ensureBackendTextureLoaded(texturePath, flipVertical);
                        },
                        &args.modelScratch->sharedTailFireAnchors,
                        false,
                        scratch.worldIndexedBatches);
                },
        });

    args.submitScratch(
        *args.camera,
        args.surfaceWidth,
        args.surfaceHeight,
        scratch,
        false,
        false);
}

} // namespace game::preview
