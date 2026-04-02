#include "vfx/preview/tackle/TacklePreviewController.h"

#include "engine/utils/LogSink.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>

#include "engine/core/Paths.h"

namespace vfx::preview::tackle {

namespace {

constexpr float kFixedDt = 1.0f / 60.0f;

glm::vec3 safeForwardXZ(const glm::vec3& value) {
    glm::vec3 forward(value.x, 0.0f, value.z);
    const float lenSq = glm::dot(forward, forward);
    if (lenSq <= 0.000001f) {
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return forward / std::sqrt(lenSq);
}

} // namespace

TacklePreviewController::TacklePreviewController(std::string_view logPrefix)
    : config_(TackleSmokeVFX::makeDefaultConfig())
    , manifestPath_(engine::paths::data("config/vfx/moves/tackle_draw_passes.json"))
    , logPrefix_(logPrefix) {}

TacklePreviewController::~TacklePreviewController() = default;

void TacklePreviewController::onActivated(engine::tools::vfx_preview::PreviewSceneState& scene) {
    scene.emitter.y = 0.42f;
    scene.target.y = 0.35f;
    ensureConfigured();
}

void TacklePreviewController::replay(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    accumulator_ = 0.0f;
    emit(scene);
}

void TacklePreviewController::reload(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    refreshManifestWriteTime();
    effect_.setConfig(config_);
    accumulator_ = 0.0f;
    emit(scene);
    log("Reloaded Tackle preview");
}

void TacklePreviewController::update(
    float dt,
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    pollManifestHotReload(scene);

    dt = std::max(0.0f, dt);
    accumulator_ += dt;
    while (accumulator_ >= kFixedDt) {
        effect_.update(kFixedDt);
        accumulator_ -= kFixedDt;
    }
}

void TacklePreviewController::stepFrames(int frames) {
    frames = std::max(0, frames);
    for (int i = 0; i < frames; ++i) {
        effect_.update(kFixedDt);
    }
}

void TacklePreviewController::render(
    const engine::tools::vfx_preview::PreviewFrameContext& frame) {
    if (!renderer_) renderer_ = std::make_unique<vfx::preview::authored::SharedAuthoredVfxRenderer>();
    renderer_->render(effect_.sharedWave(), frame.camera, frame.surfaceWidth, frame.surfaceHeight);
}

void TacklePreviewController::onResize(int width, int height) {
    if (!renderer_) renderer_ = std::make_unique<vfx::preview::authored::SharedAuthoredVfxRenderer>();
    renderer_->onResize(width, height);
}

std::uint32_t TacklePreviewController::activeCount() const {
    return effect_.activeCloudCount();
}

void TacklePreviewController::ensureConfigured() {
    effect_.setConfig(config_);
    if (!manifestWriteTime_.has_value()) {
        refreshManifestWriteTime();
    }
}

void TacklePreviewController::emit(
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    ensureConfigured();
    const glm::vec3 impactPos = scene.useCustomImpactPoint ? scene.impactPoint : scene.target;
    effect_.emitAt(impactPos, safeForwardXZ(impactPos - scene.emitter));
}

void TacklePreviewController::refreshManifestWriteTime() {
    std::error_code ec;
    if (!std::filesystem::exists(manifestPath_, ec) || ec) {
        manifestWriteTime_.reset();
        return;
    }
    manifestWriteTime_ = std::filesystem::last_write_time(manifestPath_, ec);
    if (ec) manifestWriteTime_.reset();
}

void TacklePreviewController::pollManifestHotReload(
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    std::error_code ec;
    if (!manifestWriteTime_.has_value()) {
        refreshManifestWriteTime();
    }
    if (!std::filesystem::exists(manifestPath_, ec) || ec || !manifestWriteTime_.has_value()) return;

    const auto latest = std::filesystem::last_write_time(manifestPath_, ec);
    if (ec || latest == *manifestWriteTime_) return;

    manifestWriteTime_ = latest;
    effect_.setConfig(config_);
    accumulator_ = 0.0f;
    emit(scene);
    log("Detected Tackle manifest change, hot reloaded preview");
}

void TacklePreviewController::log(const char* message) const {
    if (!message || !message[0]) return;
    engine::log::Sink log("TacklePreview", &std::cout, &std::cerr);
    if (logPrefix_.empty()) {
        log.info(message);
        return;
    }
    log.info(logPrefix_ + std::string(" ") + message);
}

} // namespace vfx::preview::tackle
