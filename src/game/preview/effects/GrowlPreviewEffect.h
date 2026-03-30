#pragma once

#include "engine/tools/vfx_preview/IVfxPreviewEffect.h"

#include <memory>

namespace vfx::preview::growl {
class GrowlPreviewController;
}

namespace game::preview {

class GrowlPreviewEffect final : public engine::tools::vfx_preview::IVfxPreviewEffect {
public:
    GrowlPreviewEffect();
    ~GrowlPreviewEffect() override;

    std::string_view name() const override;
    void onActivated(engine::tools::vfx_preview::PreviewSceneState& scene) override;
    void replay(const engine::tools::vfx_preview::PreviewSceneState& scene) override;
    void reload(const engine::tools::vfx_preview::PreviewSceneState& scene) override;
    void update(float dt, const engine::tools::vfx_preview::PreviewSceneState& scene) override;
    void stepFrames(int frames, const engine::tools::vfx_preview::PreviewSceneState& scene) override;
    void render(const engine::tools::vfx_preview::PreviewFrameContext& frame) override;
    void onResize(int width, int height) override;
    std::uint32_t activeCount() const override;
    engine::tools::vfx_preview::PreviewCasterAnimationRequest casterAnimationRequest() const override;
    std::vector<std::string> overlayLines(
        const engine::tools::vfx_preview::PreviewSceneState& scene) const override;

private:
    std::unique_ptr<vfx::preview::growl::GrowlPreviewController> controller_;
};

} // namespace game::preview
