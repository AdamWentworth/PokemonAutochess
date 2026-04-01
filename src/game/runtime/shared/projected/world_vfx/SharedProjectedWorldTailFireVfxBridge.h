#pragma once

#include "game/runtime/shared/projected/world_scene/SharedProjectedWorldSceneHelpers.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireRenderContext.h"

namespace game::runtime::shared_projected_scene::tail_fire_vfx_bridge {

game::runtime::shared_tail_fire_render::RenderContext makeRenderContext(
    const ParticleVfxArgs& args);

bool wantsAnchoredSingleFlipbook(const TailFireVFXConfig& cfg);

bool appendAnchoredSingleFlipbook(
    const ParticleVfxArgs& args,
    const TailFireVFXConfig& cfg,
    const game::runtime::shared_tail_fire_render::RenderContext& tailFireRender);

bool appendSyntheticTailFireBillboards(
    const ParticleVfxArgs& args,
    const TailFireVFXConfig& cfg,
    const game::runtime::shared_tail_fire_render::RenderContext& tailFireRender,
    bool appendedTailFireBillboards);

void appendProjectedTailFireFallbackOverlay(
    const ParticleVfxArgs& args,
    bool appendedTailFireBillboards);

} // namespace game::runtime::shared_projected_scene::tail_fire_vfx_bridge

