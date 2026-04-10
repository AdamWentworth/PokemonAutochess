#include "vfx/preview/effects/ScratchLabPreviewEffect.h"

namespace vfx::preview {

namespace {

engine::tools::vfx_preview::PreviewEffectFocusFrame makeScratchFocusFrame(
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    engine::tools::vfx_preview::PreviewEffectFocusFrame focus;
    focus.enabled = true;
    focus.center = scene.useCustomImpactPoint ? scene.impactPoint : scene.target;
    focus.radius = 0.58f;
    focus.yawDeg = -18.0f;
    focus.pitchDeg = 17.0f;
    focus.distanceMul = 0.95f;
    return focus;
}

vfx::preview::shared::ControllerBackedPreviewEffectTraits makeScratchLabTraits() {
    vfx::preview::shared::ControllerBackedPreviewEffectTraits traits;
    traits.name = "Scratch";
    traits.focusFrame = [](const engine::tools::vfx_preview::PreviewSceneState& scene) {
        return makeScratchFocusFrame(scene);
    };
    traits.overlayLines = [](const engine::tools::vfx_preview::PreviewSceneState&) {
        return std::vector<std::string>{"RenderDoc source glow cluster: EID 1208 + 1216 + 1224"};
    };
    return traits;
}

} // namespace

ScratchLabPreviewEffect::ScratchLabPreviewEffect()
    : ScratchLabPreviewEffectBase("[VfxLab]", makeScratchLabTraits()) {}

} // namespace vfx::preview
