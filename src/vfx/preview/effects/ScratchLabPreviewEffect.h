#pragma once

#include "vfx/preview/scratch/ScratchPreviewController.h"
#include "vfx/preview/shared/ControllerBackedPreviewEffect.h"

namespace vfx::preview {

using ScratchLabPreviewEffectBase = vfx::preview::shared::ControllerBackedPreviewEffect<
    vfx::preview::scratch::ScratchPreviewController>;

class ScratchLabPreviewEffect final : public ScratchLabPreviewEffectBase {
public:
    ScratchLabPreviewEffect();
};

} // namespace vfx::preview
