#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "engine/tools/vfx_preview/IVfxPreviewEffect.h"
#include "game/vfx/GrowlWaveVFX.h"

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
    void ensureConfigured();
    void emit(const engine::tools::vfx_preview::PreviewSceneState& scene);
    void refreshManifestWriteTime();
    void pollManifestHotReload(const engine::tools::vfx_preview::PreviewSceneState& scene);

    class SharedRenderer;

    GrowlWaveVFX effect_;
    GrowlWaveVFX::Config config_{};
    std::string manifestPath_;
    std::optional<std::filesystem::file_time_type> manifestWriteTime_;
    std::unique_ptr<SharedRenderer> renderer_;
    float accumulator_ = 0.0f;
    float elapsedSinceIdle_ = 0.0f;
};

} // namespace game::preview
