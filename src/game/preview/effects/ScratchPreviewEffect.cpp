#include "game/preview/effects/ScratchPreviewEffect.h"

namespace game::preview {

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

vfx::preview::shared::ControllerBackedPreviewEffectTraits makeScratchPreviewTraits() {
    vfx::preview::shared::ControllerBackedPreviewEffectTraits traits;
    traits.name = "Scratch";
    traits.casterAnimation = {
        .kind = "fast",
        .move = "scratch",
        .phase = "one_shot",
    };
    traits.previewPokemonSpecies = {
        .attackerSpecies = "charmander",
        .targetSpecies = "bulbasaur",
    };
    traits.wantsExactClipMotionPreview = true;
    traits.wantsTargetSurfaceImpactPoint = true;
    traits.afterActivated = [](engine::tools::vfx_preview::PreviewSceneState& scene) {
        scene.showOrientationGuide = false;
    };
    traits.focusFrame = [](const engine::tools::vfx_preview::PreviewSceneState& scene) {
        return makeScratchFocusFrame(scene);
    };
    traits.overlayLines = [](const engine::tools::vfx_preview::PreviewSceneState&) {
        return std::vector<std::string>{
            "Scratch preview uses the authored grouped claw-mark stack with runtime stagger/jitter.",
            "Current PAC preview mode uses local impact jitter; recoil-follow placement is still future work.",
        };
    };
    return traits;
}

} // namespace

ScratchPreviewEffect::ScratchPreviewEffect()
    : ScratchPreviewEffectBase("[VfxPreviewer]", makeScratchPreviewTraits()) {}

} // namespace game::preview
