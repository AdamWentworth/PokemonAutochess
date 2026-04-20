#include "vfx/preview/effects/LeerLabPreviewEffect.h"

namespace vfx::preview {
namespace {

engine::tools::vfx_preview::PreviewEffectFocusFrame makeLeerFocusFrame(
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    engine::tools::vfx_preview::PreviewEffectFocusFrame focus;
    focus.enabled = true;
    focus.center = scene.useCustomImpactPoint ? scene.impactPoint : scene.target;
    focus.radius = 0.90f;
    focus.yawDeg = -10.0f;
    focus.pitchDeg = 18.0f;
    focus.distanceMul = 1.10f;
    return focus;
}

vfx::preview::shared::ControllerBackedPreviewEffectTraits makeLeerLabTraits() {
    vfx::preview::shared::ControllerBackedPreviewEffectTraits traits;
    traits.name = "Leer";
    traits.focusFrame = [](const engine::tools::vfx_preview::PreviewSceneState& scene) {
        return makeLeerFocusFrame(scene);
    };
    traits.overlayLines = [](const engine::tools::vfx_preview::PreviewSceneState&) {
        return std::vector<std::string>{
            "Previewing frame 5190 / EID 1254 from RenderDoc.",
            "This pass currently uses the mesh VS-in CSV, Texture11214, and the captured TEV constants.",
            "Assumption for this first pass: mesh forward is +Z and the captured mesh is scaled by 0.10 to fit the lab scene."
        };
    };
    return traits;
}

} // namespace

LeerLabPreviewEffect::LeerLabPreviewEffect()
    : LeerLabPreviewEffectBase("[VfxLab]", makeLeerLabTraits()) {}

} // namespace vfx::preview
