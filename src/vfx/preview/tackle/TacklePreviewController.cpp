#include "vfx/preview/tackle/TacklePreviewController.h"

#include "engine/core/Paths.h"

namespace vfx::preview::tackle {

TacklePreviewController::TacklePreviewController(std::string_view logPrefix)
    : SharedPreviewControllerBase(engine::paths::data("config/vfx/moves/tackle_draw_passes.json"),
                                  "TacklePreview",
                                  "Tackle",
                                  logPrefix)
    , config_(TackleSmokeVFX::makeDefaultConfig()) {}

TacklePreviewController::~TacklePreviewController() = default;

void TacklePreviewController::configureEffect() {
    effect_.setConfig(config_);
}

void TacklePreviewController::emitScene(
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    const glm::vec3 impactPos = scene.useCustomImpactPoint ? scene.impactPoint : scene.target;
    effect_.emitAt(impactPos,
                   vfx::preview::shared::safeForwardXZ(impactPos - scene.emitter));
}

void TacklePreviewController::advanceEffect(float dt) {
    effect_.update(dt);
}

void TacklePreviewController::renderPreview(
    vfx::preview::authored::SharedAuthoredVfxRenderer& renderer,
    const engine::tools::vfx_preview::PreviewFrameContext& frame) {
    renderer.render(effect_.sharedWave(), frame.camera, frame.surfaceWidth, frame.surfaceHeight);
}

std::uint32_t TacklePreviewController::activeEffectCount() const {
    return effect_.activeCloudCount();
}

} // namespace vfx::preview::tackle
