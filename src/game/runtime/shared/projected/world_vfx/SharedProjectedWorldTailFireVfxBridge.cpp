#include "game/runtime/shared/projected/world_vfx/SharedProjectedWorldTailFireVfxBridge.h"

namespace game::runtime::shared_projected_scene::tail_fire_vfx_bridge {

namespace {

bool hasValidTailFireAnchor(
    const std::unordered_map<int, shared_tail_fire_fallback::Anchor>& anchors) {
    for (const auto& [unitId, anchor] : anchors) {
        (void)unitId;
        if (anchor.valid && !anchor.meshCarrierActive) return true;
    }
    return false;
}

} // namespace

game::runtime::shared_tail_fire_render::RenderContext makeRenderContext(
    const ParticleVfxArgs& args) {
    game::runtime::shared_tail_fire_render::RenderContext tailFireRender{};
    tailFireRender.anchors = args.sharedTailFireAnchors;
    tailFireRender.backendTextureByPath = args.backendTextureByPath;
    tailFireRender.worldIndexedBatches = args.worldIndexedBatches;
    tailFireRender.ensureBackendTextureLoaded = args.ensureBackendTextureLoaded;
    tailFireRender.useExactTailFireCpuPath = args.useExactTailFireCpuPath;
    tailFireRender.tailFireDebugEnabled = args.tailFireDebugEnabled;
    return tailFireRender;
}

bool wantsAnchoredSingleFlipbook(const TailFireVFXConfig& cfg) {
    return cfg.useFlipbook &&
           !cfg.flipbookPath.empty() &&
           !cfg.useFlipbook2;
}

bool appendAnchoredSingleFlipbook(
    const ParticleVfxArgs& args,
    const TailFireVFXConfig& cfg,
    const game::runtime::shared_tail_fire_render::RenderContext& tailFireRender) {
    if (!tailFireRender.anchors || !tailFireRender.backendTextureByPath ||
        !tailFireRender.worldIndexedBatches || !tailFireRender.ensureBackendTextureLoaded) {
        return false;
    }
    if (!wantsAnchoredSingleFlipbook(cfg)) {
        return false;
    }
    if (!hasValidTailFireAnchor(*args.sharedTailFireAnchors)) {
        return false;
    }

    ParticleSystem::RenderSnapshot snapshot{};
    snapshot.renderSettings.blend = cfg.blend;
    snapshot.renderSettings.depthTest = cfg.depthTest;
    snapshot.renderSettings.depthWrite = cfg.depthWrite;
    snapshot.pointScale = cfg.pointScale;
    snapshot.timeSec = static_cast<float>(args.simNowSec);
    snapshot.shaderVertPath = cfg.vertShaderPath;
    snapshot.shaderFragPath = cfg.fragShaderPath;
    snapshot.useFlipbook = true;
    snapshot.flipbookPath = cfg.flipbookPath;
    snapshot.flipbookCols = cfg.flipbookCols;
    snapshot.flipbookRows = cfg.flipbookRows;
    snapshot.flipbookFrames = cfg.flipbookFrames;
    snapshot.flipbookFps = cfg.flipbookFps;
    snapshot.useSecondaryFlipbook = false;

    ParticleSystem::Particle marker{};
    marker.lifeSec = 1.0f;
    marker.maxLifeSec = 1.0f;
    marker.sizePx = 0.30f;
    marker.seed = 0.0f;
    snapshot.particles.push_back(marker);

    return game::runtime::shared_tail_fire_render::appendSnapshotBillboards(
        "tail_fire_single",
        snapshot,
        args.viewProj,
        args.invViewProj,
        args.cameraWorldPos,
        args.drawableW,
        args.drawableH,
        tailFireRender);
}

bool appendSyntheticTailFireBillboards(
    const ParticleVfxArgs& args,
    const TailFireVFXConfig& cfg,
    const game::runtime::shared_tail_fire_render::RenderContext& tailFireRender,
    bool appendedTailFireBillboards) {
    if (appendedTailFireBillboards || !args.gameWorld) {
        return appendedTailFireBillboards;
    }

    game::runtime::shared_tail_fire_fallback::Args tailFireArgs;
    tailFireArgs.worldCellSize = args.worldCellSize;
    tailFireArgs.simNowSec = args.simNowSec;
    tailFireArgs.cfg = &cfg;
    tailFireArgs.anchors = args.sharedTailFireAnchors;
    tailFireArgs.pokemons = &args.gameWorld->getPokemons();
    tailFireArgs.benchPokemons = &args.gameWorld->getBenchPokemons();
    tailFireArgs.appendSnapshot =
        [&](const char* label, const ParticleSystem::RenderSnapshot& snapshot) -> bool {
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
    return game::runtime::shared_tail_fire_fallback::appendSyntheticTailFire(tailFireArgs) ||
           appendedTailFireBillboards;
}

} // namespace game::runtime::shared_projected_scene::tail_fire_vfx_bridge

