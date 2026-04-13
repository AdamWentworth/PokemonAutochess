#include "vfx/preview/effects/ScratchLabPreviewEffect.h"

namespace vfx::preview {

namespace {

engine::tools::vfx_preview::PreviewEffectFocusFrame makeScratchFocusFrame(
    const engine::tools::vfx_preview::PreviewSceneState &scene) {
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
    traits.focusFrame = [](const engine::tools::vfx_preview::PreviewSceneState &scene) {
        return makeScratchFocusFrame(scene);
    };
    traits.overlayLines = [](const engine::tools::vfx_preview::PreviewSceneState &) {
        return std::vector<std::string>{
            "RenderDoc source focus: EID 1192-1224 red glows, 1228/1232 point glow, and frame 9740 EID 1344 for the first claw mesh basis",
            "Scratch tuning: config/vfx/moves/scratch_draw_passes.json scratch_sequence pair_count, solo_first_claw_eid1032, red_glow_alpha_scale, gold_glow_alpha_scale, claw_billboard_roll_deg, angle_jitter_deg, pair_angle_deg",
            "Scratch first claw mesh: assets/meshes/scratch_frame9740_eid1344_claw_mesh.gltf"};
    };
    return traits;
}

} // namespace

ScratchLabPreviewEffect::ScratchLabPreviewEffect()
    : ScratchLabPreviewEffectBase("[VfxLab]", makeScratchLabTraits()) {}

} // namespace vfx::preview
