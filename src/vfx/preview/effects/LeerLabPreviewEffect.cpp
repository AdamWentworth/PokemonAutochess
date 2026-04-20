#include "vfx/preview/effects/LeerLabPreviewEffect.h"

namespace vfx::preview {
namespace {

engine::tools::vfx_preview::PreviewEffectFocusFrame makeLeerFocusFrame(
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    engine::tools::vfx_preview::PreviewEffectFocusFrame focus;
    focus.enabled = true;
    const glm::vec3 forward = vfx::preview::shared::safeForwardXZ(scene.target - scene.emitter);
    focus.center = scene.emitter + forward * 0.35f;
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
            "Previewing frame 5190 / right eye EIDs 1254+1268+1284+1291 and left eye EIDs 1308+1320+1336+1343.",
            "The lab currently reconstructs the captured mesh VS-in draws with Texture11214/Texture11215 plus their captured TEV constants.",
            "EIDs 1284/1291 and 1336/1343 are Texture11215 sibling meshes locally registered back onto their base eye so the vein pass layers as one surface.",
            "Each four-pass eye is offset in emitter-local space so the pair sits side-by-side with a capture-inspired gap instead of overlapping.",
            "Leer is source-aligned like Growl: it emits from the caster and uses the target only to choose forward direction.",
            "Current assumptions: right-eye authored forward is -Z, left-eye authored forward is +Z, scale is 0.10, and dual-UV passes use rawtex0 because rawtex1 matches it."
        };
    };
    return traits;
}

} // namespace

LeerLabPreviewEffect::LeerLabPreviewEffect()
    : LeerLabPreviewEffectBase("[VfxLab]", makeLeerLabTraits()) {}

} // namespace vfx::preview
