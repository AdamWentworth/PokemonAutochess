#pragma once

#include <cstdint>
#include <string_view>

#include "vfx/effects/growl/GrowlWaveVFX.h"
#include "vfx/preview/shared/SharedPreviewControllerBase.h"

namespace vfx::preview::growl {

class GrowlPreviewController final
    : public vfx::preview::shared::SharedPreviewControllerBase<GrowlPreviewController> {
public:
    explicit GrowlPreviewController(std::string_view logPrefix);
    ~GrowlPreviewController();

    void configureEffect();
    void emitScene(const engine::tools::vfx_preview::PreviewSceneState& scene);
    void advanceEffect(float dt);
    void renderPreview(vfx::preview::authored::SharedAuthoredVfxRenderer& renderer,
                       const engine::tools::vfx_preview::PreviewFrameContext& frame);
    std::uint32_t activeEffectCount() const;

private:
    GrowlWaveVFX effect_;
    GrowlWaveVFX::Config config_{};
};

} // namespace vfx::preview::growl
