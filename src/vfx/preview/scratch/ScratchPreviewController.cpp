#include "vfx/preview/scratch/ScratchPreviewController.h"

#include "engine/core/Paths.h"

namespace vfx::preview::scratch {

ScratchPreviewController::ScratchPreviewController(std::string_view logPrefix)
    : SharedPreviewControllerBase(engine::paths::data("config/vfx/moves/scratch_draw_passes.json"),
                                  "ScratchPreview",
                                  "Scratch",
                                  logPrefix)
    , config_(ScratchGlowVFX::makeDefaultConfig()) {}

ScratchPreviewController::~ScratchPreviewController() = default;

void ScratchPreviewController::configureEffect() {
    effect_.setConfig(config_);
}

void ScratchPreviewController::emitScene(
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    const glm::vec3 impactPos = scene.useCustomImpactPoint ? scene.impactPoint : scene.target;
    effect_.emitAt(impactPos,
                   vfx::preview::shared::safeForwardXZ(impactPos - scene.emitter),
                   lastViewMatrix());
}

void ScratchPreviewController::advanceEffect(float dt) {
    effect_.update(dt);
}

void ScratchPreviewController::renderPreview(
    vfx::preview::authored::SharedAuthoredVfxRenderer& renderer,
    const engine::tools::vfx_preview::PreviewFrameContext& frame) {
    renderer.render(effect_.sharedGlow(), frame.camera, frame.surfaceWidth, frame.surfaceHeight);
}

std::uint32_t ScratchPreviewController::activeEffectCount() const {
    return effect_.activeGlowCount();
}

} // namespace vfx::preview::scratch
