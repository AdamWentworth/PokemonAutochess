#include "game/preview/effects/TacklePreviewEffect.h"

namespace game::preview {

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

vfx::preview::shared::ControllerBackedPreviewEffectTraits makeTacklePreviewTraits() {
    vfx::preview::shared::ControllerBackedPreviewEffectTraits traits;
    traits.name = "Tackle";
    traits.casterAnimation = {
        .kind = "fast",
        .move = "tackle",
        .phase = "one_shot",
    };
    traits.previewPokemonSpecies = {
        .attackerSpecies = "bulbasaur",
        .targetSpecies = "charmander",
    };
    traits.wantsExactClipMotionPreview = true;
    traits.wantsTargetSurfaceImpactPoint = true;
    traits.afterActivated = [](engine::tools::vfx_preview::PreviewSceneState& scene) {
        scene.showOrientationGuide = false;
    };
    traits.focusFrame = [](const engine::tools::vfx_preview::PreviewSceneState& scene) {
        return makeTackleFocusFrame(scene);
    };
    traits.overlayLines = [](const engine::tools::vfx_preview::PreviewSceneState&) {
        return std::vector<std::string>{
        "Tackle uses the shared authored-batch path with alpha-blended smoke billboards."
        };
    };
    return traits;
}

} // namespace

TacklePreviewEffect::TacklePreviewEffect()
    : TacklePreviewEffectBase("[VfxPreviewer]", makeTacklePreviewTraits()) {}

} // namespace game::preview
