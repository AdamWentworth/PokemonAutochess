#include "vfx/preview/effects/ScratchLabPreviewEffect.h"

namespace vfx::preview {

namespace {

engine::tools::vfx_preview::PreviewEffectFocusFrame makeScratchFocusFrame(
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    engine::tools::vfx_preview::PreviewEffectFocusFrame focus;
    focus.enabled = true;
    focus.center = scene.useCustomImpactPoint ? scene.impactPoint : scene.target;
    focus.radius = 0.48f;
    focus.yawDeg = -16.0f;
    focus.pitchDeg = 14.0f;
    focus.distanceMul = 1.00f;
    return focus;
}

vfx::preview::shared::ControllerBackedPreviewEffectTraits makeScratchLabTraits() {
    vfx::preview::shared::ControllerBackedPreviewEffectTraits traits;
    traits.name = "Scratch";
    traits.focusFrame = [](const engine::tools::vfx_preview::PreviewSceneState& scene) {
        return makeScratchFocusFrame(scene);
    };
    return traits;
}

} // namespace

ScratchLabPreviewEffect::ScratchLabPreviewEffect()
    : ScratchLabPreviewEffectBase("[VfxLab]", makeScratchLabTraits()) {}

} // namespace vfx::preview
