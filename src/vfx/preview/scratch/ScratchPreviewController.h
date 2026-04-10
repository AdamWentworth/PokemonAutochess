#pragma once

#include <cstdint>
#include <string_view>

#include "vfx/effects/shared/SharedAuthoredBatchVFX.h"
#include "vfx/preview/shared/SharedPreviewControllerBase.h"

namespace vfx::preview::scratch {

class ScratchPreviewController final
    : public vfx::preview::shared::SharedPreviewControllerBase<ScratchPreviewController> {
public:
    explicit ScratchPreviewController(std::string_view logPrefix);
    ~ScratchPreviewController();

    void configureEffect();
    void emitScene(const engine::tools::vfx_preview::PreviewSceneState& scene);
    void advanceEffect(float dt);
    void renderPreview(vfx::preview::authored::SharedAuthoredVfxRenderer& renderer,
                       const engine::tools::vfx_preview::PreviewFrameContext& frame);
    std::uint32_t activeEffectCount() const;

private:
    SharedAuthoredBatchVFX effect_;
    SharedAuthoredBatchVFX::Config config_{};
};

} // namespace vfx::preview::scratch
