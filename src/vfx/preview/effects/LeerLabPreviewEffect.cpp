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
            "Previewing frame 5190 / right eye EIDs 1254+1268+1284+1291, left eye EIDs 1308+1320+1336+1343, and pupil highlights EIDs 1355..1432.",
            "The lab currently reconstructs the captured mesh VS-in draws with Texture11214/Texture11215 plus the captured Texture11230 highlight family.",
            "EIDs 1284/1291 and 1336/1343 are Texture11215 sibling meshes locally registered back onto their base eye so the vein pass layers as one surface.",
            "Each four-pass eye is offset in emitter-local space so the pair sits side-by-side with a capture-inspired gap instead of overlapping.",
            "The 1355..1432 highlight family is now split into per-eye strip meshes so each pupil highlight can reuse the established right/left eye placement instead of over-scaling one shared pair.",
            "Leer is source-aligned like Growl: it emits from the caster and uses the target only to choose forward direction.",
            "Current assumptions: right-eye authored forward is -Z, left-eye authored forward is +Z, split pupil strips use their captured local plane normal so they lie flat on each eye, and Texture11230 uses identity UVs plus a softened captured dual-source additive TEV ramp."
        };
    };
    return traits;
}

} // namespace

LeerLabPreviewEffect::LeerLabPreviewEffect()
    : LeerLabPreviewEffectBase("[VfxLab]", makeLeerLabTraits()) {}

} // namespace vfx::preview
