#pragma once

#include "vfx/preview/growl/GrowlPreviewController.h"
#include "vfx/preview/shared/ControllerBackedPreviewEffect.h"

namespace vfx::preview {

using GrowlLabPreviewEffectBase = vfx::preview::shared::ControllerBackedPreviewEffect<
    vfx::preview::growl::GrowlPreviewController>;

class GrowlLabPreviewEffect final : public GrowlLabPreviewEffectBase {
public:
    GrowlLabPreviewEffect();
};

} // namespace vfx::preview
