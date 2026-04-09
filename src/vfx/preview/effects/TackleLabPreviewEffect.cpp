#include "vfx/preview/effects/TackleLabPreviewEffect.h"

namespace vfx::preview {

namespace {

engine::tools::vfx_preview::PreviewEffectFocusFrame makeTackleFocusFrame(
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    engine::tools::vfx_preview::PreviewEffectFocusFrame focus;
    focus.enabled = true;
    focus.center = scene.useCustomImpactPoint ? scene.impactPoint : scene.target;
    focus.radius = 0.62f;
    focus.yawDeg = -12.0f;
    focus.pitchDeg = 16.0f;
    focus.distanceMul = 1.05f;
    return focus;
}

vfx::preview::shared::ControllerBackedPreviewEffectTraits makeTackleLabTraits() {
    vfx::preview::shared::ControllerBackedPreviewEffectTraits traits;
    traits.name = "Tackle";
    traits.focusFrame = [](const engine::tools::vfx_preview::PreviewSceneState& scene) {
        return makeTackleFocusFrame(scene);
    };
    return traits;
}

} // namespace

TackleLabPreviewEffect::TackleLabPreviewEffect()
    : TackleLabPreviewEffectBase("[VfxLab]", makeTackleLabTraits()) {}

} // namespace vfx::preview
