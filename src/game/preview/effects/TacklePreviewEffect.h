#pragma once

#include "vfx/preview/shared/ControllerBackedPreviewEffect.h"
#include "vfx/preview/tackle/TacklePreviewController.h"

namespace game::preview {

using TacklePreviewEffectBase = vfx::preview::shared::ControllerBackedPreviewEffect<
    vfx::preview::tackle::TacklePreviewController>;

class TacklePreviewEffect final : public TacklePreviewEffectBase {
public:
    TacklePreviewEffect();
};

} // namespace game::preview
