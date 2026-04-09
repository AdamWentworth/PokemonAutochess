#pragma once

#include <cstdint>
#include <string_view>

#include "vfx/effects/tackle/TackleSmokeVFX.h"
#include "vfx/preview/shared/SharedPreviewControllerBase.h"

namespace vfx::preview::tackle {

class TacklePreviewController final
    : public vfx::preview::shared::SharedPreviewControllerBase<TacklePreviewController> {
public:
    explicit TacklePreviewController(std::string_view logPrefix);
    ~TacklePreviewController();

    void configureEffect();
    void emitScene(const engine::tools::vfx_preview::PreviewSceneState& scene);
    void advanceEffect(float dt);
    void renderPreview(vfx::preview::authored::SharedAuthoredVfxRenderer& renderer,
                       const engine::tools::vfx_preview::PreviewFrameContext& frame);
    std::uint32_t activeEffectCount() const;

private:
    TackleSmokeVFX effect_;
    TackleSmokeVFX::Config config_{};
};

} // namespace vfx::preview::tackle
