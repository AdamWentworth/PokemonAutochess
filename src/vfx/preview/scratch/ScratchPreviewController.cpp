#include "engine/render/Model.h"
#include "engine/core/Paths.h"
#include "vfx/preview/scratch/ScratchPreviewController.h"

namespace vfx::preview::scratch {

namespace {

SharedAuthoredBatchVFX::Config makeScratchPreviewConfig() {
    SharedAuthoredBatchVFX::Config config{};
    config.spawnForwardOffset = 0.0f;
    config.spawnHeightOffset = 0.0f;
    config.ringForwardOffset = 0.0f;
    config.ringMinSpeed = 0.0f;
    config.ringMaxSpeed = 0.0f;
    config.ringMinLifeSec = 0.45f;
    config.ringMaxLifeSec = 0.45f;
    config.ringMinSize = 1.0f;
    config.ringMaxSize = 1.0f;
    config.ringTrailCount = 0;
    config.ringScaleGrowth = 1.0f;
    config.fadeStart = 0.6f;
    config.vertShaderPath = "assets/shaders/vfx/moves/scratch/scratch_flash_shared.vert";
    config.fragShaderPath = "assets/shaders/vfx/moves/scratch/scratch_flash_shared.frag";
    config.tevC0 = glm::vec3(1.0f);
    config.tevC1 = glm::vec3(0.0f);
    config.tevK0 = glm::vec3(1.0f);
    config.tevC0A = 1.0f;
    config.tevC1A = 0.0f;
    config.tevK1A = 1.0f;
    config.blendMode = 1u;
    config.drawManifestPath = "config/vfx/moves/scratch_draw_passes.json";
    config.drawPasses.clear();
    config.depthTest = true;
    config.depthWrite = false;
    return config;
}

} // namespace

ScratchPreviewController::ScratchPreviewController(std::string_view logPrefix)
    : SharedPreviewControllerBase(engine::paths::data("config/vfx/moves/scratch_draw_passes.json"),
                                  "ScratchPreview",
                                  "Scratch",
                                  logPrefix) {
    config_ = makeScratchPreviewConfig();
}

ScratchPreviewController::~ScratchPreviewController() = default;

void ScratchPreviewController::configureEffect() {
    effect_.setConfig(config_);
}

void ScratchPreviewController::emitScene(
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    const glm::vec3 impactPos = scene.useCustomImpactPoint ? scene.impactPoint : scene.target;
    effect_.emitFrom(impactPos,
                     vfx::preview::shared::safeForwardXZ(impactPos - scene.emitter),
                     nullptr);
}

void ScratchPreviewController::advanceEffect(float dt) {
    effect_.update(dt);
}

void ScratchPreviewController::renderPreview(
    vfx::preview::authored::SharedAuthoredVfxRenderer& renderer,
    const engine::tools::vfx_preview::PreviewFrameContext& frame) {
    renderer.render(effect_, frame.camera, frame.surfaceWidth, frame.surfaceHeight);
}

std::uint32_t ScratchPreviewController::activeEffectCount() const {
    return effect_.activeRingCount();
}

} // namespace vfx::preview::scratch
