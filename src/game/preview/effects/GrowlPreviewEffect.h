#pragma once

#include "vfx/preview/growl/GrowlPreviewController.h"
#include "vfx/preview/shared/ControllerBackedPreviewEffect.h"

namespace game::preview {

using GrowlPreviewEffectBase = vfx::preview::shared::ControllerBackedPreviewEffect<
    vfx::preview::growl::GrowlPreviewController>;

class GrowlPreviewEffect final : public GrowlPreviewEffectBase {
public:
    GrowlPreviewEffect();
};

} // namespace game::preview
