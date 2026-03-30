#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "engine/render/Model.h"
#include "engine/tools/vfx_preview/IVfxPreviewEffect.h"
#include "vfx/effects/growl/GrowlWaveVFX.h"

namespace vfx::preview {

namespace growl {
class GrowlSharedRenderer;
}

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
    void ensureConfigured();
    void emit(const engine::tools::vfx_preview::PreviewSceneState& scene);
    void refreshManifestWriteTime();
    void pollManifestHotReload(const engine::tools::vfx_preview::PreviewSceneState& scene);

    GrowlWaveVFX effect_;
    GrowlWaveVFX::Config config_{};    
    std::string manifestPath_;
    std::optional<std::filesystem::file_time_type> manifestWriteTime_;
    std::unique_ptr<growl::GrowlSharedRenderer> renderer_;
    float accumulator_ = 0.0f;
};

} // namespace vfx::preview
