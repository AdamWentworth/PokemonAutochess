#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "engine/tools/vfx_preview/VfxPreviewTypes.h"
#include "vfx/effects/growl/GrowlWaveVFX.h"

namespace vfx::preview::growl {

class GrowlSharedRenderer;

class GrowlPreviewController {
public:
    explicit GrowlPreviewController(std::string_view logPrefix);
    ~GrowlPreviewController();

    void onActivated(engine::tools::vfx_preview::PreviewSceneState& scene);
    void replay(const engine::tools::vfx_preview::PreviewSceneState& scene);
    void reload(const engine::tools::vfx_preview::PreviewSceneState& scene);
    void update(float dt, const engine::tools::vfx_preview::PreviewSceneState& scene);
    void stepFrames(int frames);
    void render(const engine::tools::vfx_preview::PreviewFrameContext& frame);
    void onResize(int width, int height);
    std::uint32_t activeCount() const;

private:
    void ensureConfigured();
    void emit(const engine::tools::vfx_preview::PreviewSceneState& scene);
    void refreshManifestWriteTime();
    void pollManifestHotReload(const engine::tools::vfx_preview::PreviewSceneState& scene);
    void log(const char* message) const;

    GrowlWaveVFX effect_;
    GrowlWaveVFX::Config config_{};
    std::string manifestPath_;
    std::optional<std::filesystem::file_time_type> manifestWriteTime_;
    std::unique_ptr<GrowlSharedRenderer> renderer_;
    std::string logPrefix_;
    float accumulator_ = 0.0f;
};

} // namespace vfx::preview::growl
