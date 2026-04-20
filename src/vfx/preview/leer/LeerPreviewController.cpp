#include "vfx/preview/leer/LeerPreviewController.h"

#include "engine/core/Paths.h"
#include "engine/render/Model.h"
#include "engine/utils/Shader.h"

namespace vfx::preview::leer {
namespace {

constexpr float kLeerDurationSec = 50.0f / 30.0f;

SharedAuthoredBatchVFX::Config makeLeerLabConfig() {
    SharedAuthoredBatchVFX::Config config{};
    config.spawnForwardOffset = 0.0f;
    config.spawnHeightOffset = 0.0f;
    config.ringForwardOffset = 0.0f;
    config.ringMinSpeed = 0.0f;
    config.ringMaxSpeed = 0.0f;
    config.ringMinLifeSec = kLeerDurationSec;
    config.ringMaxLifeSec = kLeerDurationSec;
    config.ringMinSize = 1.0f;
    config.ringMaxSize = 1.0f;
    config.ringTrailCount = 0;
    config.ringScaleGrowth = 1.0f;
    config.fadeStart = 0.92f;
    config.vertShaderPath = "assets/shaders/vfx/moves/leer/leer_eid_1254_vertex_renderdoc.glsl";
    config.fragShaderPath = "assets/shaders/vfx/moves/leer/leer_eid_1254_fragment_renderdoc.glsl";
    config.tevC0 = glm::vec3(1.0f, 1.0f, 1.0f);
    config.tevC1 = glm::vec3(0.4f, 0.039216f, 0.2f);
    config.tevK0 = glm::vec3(0.8f, 0.2f, 0.298039f);
    config.tevK1A = 1.0f;
    config.blendMode = 0u;
    config.drawManifestPath = "config/vfx/moves/leer_draw_passes.json";
    config.drawPasses.clear();
    config.meshForwardAxis = glm::vec3(0.0f, 0.0f, -1.0f);
    config.depthTest = true;
    config.depthWrite = false;
    return config;
}

} // namespace

LeerPreviewController::LeerPreviewController(std::string_view logPrefix)
    : SharedPreviewControllerBase(engine::paths::data("config/vfx/moves/leer_draw_passes.json"),
                                  "LeerPreview",
                                  "Leer",
                                  logPrefix)
    , config_(makeLeerLabConfig()) {}

LeerPreviewController::~LeerPreviewController() = default;

void LeerPreviewController::configureEffect() {
    effect_.setConfig(config_);
}

void LeerPreviewController::emitScene(
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    effect_.emitFrom(scene.emitter,
                     vfx::preview::shared::safeForwardXZ(scene.target - scene.emitter),
                     nullptr);
}

void LeerPreviewController::advanceEffect(float dt) {
    effect_.update(dt);
}

void LeerPreviewController::renderPreview(
    vfx::preview::authored::SharedAuthoredVfxRenderer& renderer,
    const engine::tools::vfx_preview::PreviewFrameContext& frame) {
    renderer.render(effect_, frame.camera, frame.surfaceWidth, frame.surfaceHeight);
}

std::uint32_t LeerPreviewController::activeEffectCount() const {
    return effect_.activeRingCount();
}

} // namespace vfx::preview::leer
