#include "vfx/preview/growl/GrowlPreviewController.h"

#include "engine/core/Paths.h"
#include "engine/render/Model.h"

namespace vfx::preview::growl {

GrowlPreviewController::GrowlPreviewController(std::string_view logPrefix)
    : SharedPreviewControllerBase(engine::paths::data("config/vfx/moves/growl_draw_passes.json"),
                                  "GrowlPreview",
                                  "Growl",
                                  logPrefix) {
    config_.spawnForwardOffset = 0.0f;
    config_.spawnHeightOffset = 0.0f;
    config_.drawManifestPath = "config/vfx/moves/growl_draw_passes.json";
}

GrowlPreviewController::~GrowlPreviewController() = default;

void GrowlPreviewController::configureEffect() {
    effect_.setConfig(config_);
}

void GrowlPreviewController::emitScene(
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    effect_.emitFrom(scene.emitter,
                     vfx::preview::shared::safeForwardXZ(scene.target - scene.emitter),
                     nullptr);
}

void GrowlPreviewController::advanceEffect(float dt) {
    effect_.update(dt);
}

void GrowlPreviewController::renderPreview(
    vfx::preview::authored::SharedAuthoredVfxRenderer& renderer,
    const engine::tools::vfx_preview::PreviewFrameContext& frame) {
    renderer.render(effect_, frame.camera, frame.surfaceWidth, frame.surfaceHeight);
}

std::uint32_t GrowlPreviewController::activeEffectCount() const {
    return effect_.activeRingCount();
}

} // namespace vfx::preview::growl
