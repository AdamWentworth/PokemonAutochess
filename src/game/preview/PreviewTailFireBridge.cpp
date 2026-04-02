#include "game/preview/PreviewTailFireBridge.h"

#include <glm/gtc/matrix_inverse.hpp>

#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"
#include "game/runtime/shared/projected/world_vfx/SharedProjectedWorldTailFireVfxBridge.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireFallbackEmitter.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireRenderContext.h"

namespace game::preview {

void renderPreviewTailFire(const PreviewTailFireBridgeArgs& args) {
    if (!args.camera ||
        !args.visual ||
        !args.visual->valid ||
        !args.backendTextureByPath ||
        !args.modelScratch ||
        !args.tailFireScratch ||
        !args.buildProjectedModelScratch ||
        !args.submitScratch) {
        return;
    }

    (void)args.buildProjectedModelScratch(
        *args.camera,
        args.surfaceWidth,
        args.surfaceHeight,
        *args.visual,
        args.worldPos,
        args.yawDeg,
        args.side);

    if (!args.fallbackConfig) {
        return;
    }

    auto& scratch = *args.tailFireScratch;
    game::runtime::session_render_scratch::ensureCapacity(scratch);
    game::runtime::session_render_scratch::beginFrame(scratch, true, args.renderer);

    std::unordered_map<int, game::runtime::shared_tail_fire_fallback::Anchor> previewAnchors =
        args.modelScratch->sharedTailFireAnchors;
    for (auto& [unitId, anchor] : previewAnchors) {
        (void)unitId;
        if (!anchor.valid) continue;
        // Preview intentionally does not render the authored Tail Fire mesh
        // carrier. Keep the exported attachment frame, but force the preview
        // fire path to treat it as a billboard-capable anchor.
        anchor.meshCarrierActive = false;
    }

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
    game::runtime::shared_tail_fire_render::RenderContext tailFireRender{};
    tailFireRender.anchors = &previewAnchors;
    tailFireRender.backendTextureByPath = args.backendTextureByPath;
    tailFireRender.worldIndexedBatches = &scratch.worldIndexedBatches;
    tailFireRender.ensureBackendTextureLoaded = args.ensureBackendTextureLoaded;
    tailFireRender.tailFireDebugEnabled = false;

    game::runtime::shared_projected_scene::ParticleVfxArgs particleVfxArgs{};
    particleVfxArgs.viewProj = viewProj;
    particleVfxArgs.invViewProj = invViewProj;
    particleVfxArgs.cameraWorldPos = args.camera->getPosition();
    particleVfxArgs.drawableW = args.surfaceWidth;
    particleVfxArgs.drawableH = args.surfaceHeight;
    particleVfxArgs.worldCellSize = args.worldCellSize;
    particleVfxArgs.simNowSec = args.simNowSec;
    particleVfxArgs.sharedTailFireAnchors = &previewAnchors;
    particleVfxArgs.backendTextureByPath = args.backendTextureByPath;
    particleVfxArgs.worldIndexedBatches = &scratch.worldIndexedBatches;
    particleVfxArgs.ensureBackendTextureLoaded = args.ensureBackendTextureLoaded;

    bool appendedTailFireBillboards = false;
    if (game::runtime::shared_projected_scene::tail_fire_vfx_bridge::wantsAnchoredSingleFlipbook(
            *args.fallbackConfig)) {
        appendedTailFireBillboards =
            game::runtime::shared_projected_scene::tail_fire_vfx_bridge::appendAnchoredSingleFlipbook(
                particleVfxArgs,
                *args.fallbackConfig,
                tailFireRender);
    }

    if (!appendedTailFireBillboards) {
        appendedTailFireBillboards =
            game::runtime::shared_tail_fire_fallback::appendSyntheticTailFire(
                {
                    .worldCellSize = args.worldCellSize,
                    .simNowSec = args.simNowSec,
                    .cfg = args.fallbackConfig,
                    .anchors = &previewAnchors,
                    .pokemons = &boardUnits,
                    .benchPokemons = &kNoBenchUnits,
                    .appendSnapshot =
                        [&](const char* label, const ParticleSystem::RenderSnapshot& snapshot) -> bool {
                            return game::runtime::shared_tail_fire_render::appendSnapshotBillboards(
                                label,
                                snapshot,
                                viewProj,
                                invViewProj,
                                args.camera->getPosition(),
                                args.surfaceWidth,
                                args.surfaceHeight,
                                tailFireRender);
                        },
                });
    }

    if (!appendedTailFireBillboards) {
        return;
    }

    args.submitScratch(
        *args.camera,
        args.surfaceWidth,
        args.surfaceHeight,
        scratch,
        false,
        false);
}

} // namespace game::preview
