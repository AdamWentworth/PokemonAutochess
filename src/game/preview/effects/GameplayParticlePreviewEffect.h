#pragma once

#include "engine/tools/vfx_preview/IVfxPreviewEffect.h"
#include "game/runtime/session/SessionTextureCache.h"
#include "game/runtime/shared/vfx/tail_fire/SharedTailFireSyntheticEmitter.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <memory>
#include <string>
#include <vector>

class AquaSwooshVFX;
class ClawSwipeVFX;
class GrassImpactVFX;
class HealPlusVFX;
class LeechSeedDrainVFX;
class TackleImpactVFX;

namespace game::preview {

class GameplayParticlePreviewEffect final
    : public engine::tools::vfx_preview::
          IVfxPreviewEffect {
public:
    enum class Kind {
        AquaSwoosh,
        ClawSwipe,
        GrassImpact,
        HealPlus,
        LeechSeedDrain,
        TackleImpact,
        TailFire,
    };

    explicit GameplayParticlePreviewEffect(Kind kind);
    ~GameplayParticlePreviewEffect() override;

    std::string_view name() const override;
    void onActivated(
        engine::tools::vfx_preview::
            PreviewSceneState& scene) override;
    void replay(
        const engine::tools::vfx_preview::
            PreviewSceneState& scene) override;
    void update(
        float dt,
        const engine::tools::vfx_preview::
            PreviewSceneState& scene) override;
    void stepFrames(
        int frames,
        const engine::tools::vfx_preview::
            PreviewSceneState& scene) override;
    void render(
        const engine::tools::vfx_preview::
            PreviewFrameContext& frame) override;
    std::uint32_t activeCount() const override;
    engine::tools::vfx_preview::
        PreviewEffectFocusFrame
    previewFocusFrame(
        const engine::tools::vfx_preview::
            PreviewSceneState& scene) const override;

private:
    void appendSnapshots(
        std::vector<ParticleSystem::RenderSnapshot>&
            snapshots) const;
    void renderSnapshots(
        const std::vector<ParticleSystem::RenderSnapshot>&
            snapshots,
        const engine::tools::vfx_preview::
            PreviewFrameContext& frame);
    void updateTailFire(float dt);

    Kind kind_;
    std::string name_;
    std::unique_ptr<AquaSwooshVFX> aqua_;
    std::unique_ptr<ClawSwipeVFX> claw_;
    std::unique_ptr<GrassImpactVFX> grass_;
    std::unique_ptr<HealPlusVFX> heal_;
    std::unique_ptr<LeechSeedDrainVFX> drain_;
    std::unique_ptr<TackleImpactVFX> tackleImpact_;
    game::runtime::shared_tail_fire_synth_emitter::
        SyntheticEmitterState tailFire_;
    TailFireVFXConfig tailFireConfig_{};
    float tailFireTimeSeconds_ = 0.0f;
    game::runtime::session_texture_cache::TextureCache
        textureCache_;
    std::vector<
        game::runtime::shared_world_batches::
            WorldIndexedBatch>
        batches_;
};

} // namespace game::preview
