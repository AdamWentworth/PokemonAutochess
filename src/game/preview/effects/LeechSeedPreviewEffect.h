#pragma once

#include "engine/tools/vfx_preview/IVfxPreviewEffect.h"
#include "game/runtime/session/SessionTextureCache.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "game/vfx/LeechSeedProjectileVFX.h"

#include <vector>

namespace game::preview {

class LeechSeedPreviewEffect final : public engine::tools::vfx_preview::IVfxPreviewEffect {
public:
    std::string_view name() const override;
    void onActivated(engine::tools::vfx_preview::PreviewSceneState& scene) override;
    void replay(const engine::tools::vfx_preview::PreviewSceneState& scene) override;
    void update(float dt, const engine::tools::vfx_preview::PreviewSceneState& scene) override;
    void stepFrames(int frames, const engine::tools::vfx_preview::PreviewSceneState& scene) override;
    void render(const engine::tools::vfx_preview::PreviewFrameContext& frame) override;
    std::uint32_t activeCount() const override;
    engine::tools::vfx_preview::PreviewActorSelection previewActors() const override;
    std::vector<std::string> overlayLines(
        const engine::tools::vfx_preview::PreviewSceneState& scene) const override;

private:
    void emit(const engine::tools::vfx_preview::PreviewSceneState& scene);

    LeechSeedProjectileVFX effect_;
    game::runtime::session_texture_cache::TextureCache
        textureCache_;
    std::vector<
        game::runtime::shared_world_batches::
            WorldIndexedBatch>
        batches_;
};

} // namespace game::preview
