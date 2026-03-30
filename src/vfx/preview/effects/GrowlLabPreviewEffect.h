#pragma once

#include <memory>

#include "engine/tools/vfx_preview/IVfxPreviewEffect.h"

namespace vfx::preview::growl {
class GrowlPreviewController;
}

namespace vfx::preview {

class GrowlLabPreviewEffect final : public engine::tools::vfx_preview::IVfxPreviewEffect {
public:
    GrowlLabPreviewEffect();
    ~GrowlLabPreviewEffect() override;

    std::string_view name() const override;
    void onActivated(engine::tools::vfx_preview::PreviewSceneState& scene) override;
    void replay(const engine::tools::vfx_preview::PreviewSceneState& scene) override;
    void reload(const engine::tools::vfx_preview::PreviewSceneState& scene) override;
    void update(float dt, const engine::tools::vfx_preview::PreviewSceneState& scene) override;
    void stepFrames(int frames, const engine::tools::vfx_preview::PreviewSceneState& scene) override;
    void render(const engine::tools::vfx_preview::PreviewFrameContext& frame) override;
    void onResize(int width, int height) override;
    std::uint32_t activeCount() const override;
    std::vector<std::string> overlayLines(
        const engine::tools::vfx_preview::PreviewSceneState& scene) const override;

private:
    std::unique_ptr<growl::GrowlPreviewController> controller_;
};

} // namespace vfx::preview
