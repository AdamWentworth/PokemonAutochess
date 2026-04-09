#include "vfx/preview/effects/GrowlLabPreviewEffect.h"

namespace vfx::preview {

namespace {

vfx::preview::shared::ControllerBackedPreviewEffectTraits makeGrowlLabTraits() {
    vfx::preview::shared::ControllerBackedPreviewEffectTraits traits;
    traits.name = "Growl";
    return traits;
}

} // namespace

GrowlLabPreviewEffect::GrowlLabPreviewEffect()
    : GrowlLabPreviewEffectBase("[VfxLab]", makeGrowlLabTraits()) {}

} // namespace vfx::preview
