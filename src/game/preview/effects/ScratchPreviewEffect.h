#pragma once

#include "vfx/preview/shared/ControllerBackedPreviewEffect.h"
#include "vfx/preview/scratch/ScratchPreviewController.h"

namespace game::preview {

using ScratchPreviewEffectBase = vfx::preview::shared::ControllerBackedPreviewEffect<
    vfx::preview::scratch::ScratchPreviewController>;

class ScratchPreviewEffect final : public ScratchPreviewEffectBase {
public:
    ScratchPreviewEffect();
};

} // namespace game::preview
