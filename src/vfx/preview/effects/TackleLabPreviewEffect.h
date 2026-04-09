#pragma once

#include "vfx/preview/shared/ControllerBackedPreviewEffect.h"
#include "vfx/preview/tackle/TacklePreviewController.h"

namespace vfx::preview {

using TackleLabPreviewEffectBase = vfx::preview::shared::ControllerBackedPreviewEffect<
    vfx::preview::tackle::TacklePreviewController>;

class TackleLabPreviewEffect final : public TackleLabPreviewEffectBase {
public:
    TackleLabPreviewEffect();
};

} // namespace vfx::preview
