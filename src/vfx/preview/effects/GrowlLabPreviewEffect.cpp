#include "vfx/preview/effects/GrowlLabPreviewEffect.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

#include "engine/core/Paths.h"
#include "engine/render/Camera3D.h"
#include "vfx/preview/growl/GrowlSharedRenderer.h"

namespace vfx::preview {

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

GrowlLabPreviewEffect::GrowlLabPreviewEffect()
    : manifestPath_(engine::paths::data("config/vfx/moves/growl_draw_passes.json")) {
    config_.spawnForwardOffset = 0.0f;
    config_.spawnHeightOffset = 0.0f;
    config_.drawManifestPath = "config/vfx/moves/growl_draw_passes.json";
}

GrowlLabPreviewEffect::~GrowlLabPreviewEffect() = default;

std::string_view GrowlLabPreviewEffect::name() const {
    return "Growl";
}

void GrowlLabPreviewEffect::ensureConfigured() {
    effect_.setConfig(config_);
    if (!manifestWriteTime_.has_value()) {
        refreshManifestWriteTime();
    }
}

void GrowlLabPreviewEffect::onActivated(engine::tools::vfx_preview::PreviewSceneState& scene) {
    scene.emitter.y = 0.42f;
    scene.target.y = 0.35f;
    ensureConfigured();
}

void GrowlLabPreviewEffect::emit(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    ensureConfigured();
    effect_.emitFrom(scene.emitter, safeForwardXZ(scene.target - scene.emitter), nullptr);
}

void GrowlLabPreviewEffect::replay(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    accumulator_ = 0.0f;
    emit(scene);
}

void GrowlLabPreviewEffect::reload(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    refreshManifestWriteTime();
    effect_.setConfig(config_);
    accumulator_ = 0.0f;
    emit(scene);
    std::cout << "[VfxLab] Reloaded Growl preview\n";
}

void GrowlLabPreviewEffect::update(float dt, const engine::tools::vfx_preview::PreviewSceneState& scene) {
    pollManifestHotReload(scene);

    dt = std::max(0.0f, dt);
    accumulator_ += dt;
    while (accumulator_ >= kFixedDt) {
        effect_.update(kFixedDt);
        accumulator_ -= kFixedDt;
    }
}

void GrowlLabPreviewEffect::stepFrames(
    int frames,
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    (void)scene;
    frames = std::max(0, frames);
    for (int i = 0; i < frames; ++i) {
        effect_.update(kFixedDt);
    }
}

void GrowlLabPreviewEffect::render(const engine::tools::vfx_preview::PreviewFrameContext& frame) {
    if (!renderer_) renderer_ = std::make_unique<growl::GrowlSharedRenderer>();
    renderer_->render(effect_, frame.camera, frame.surfaceWidth, frame.surfaceHeight);
}

void GrowlLabPreviewEffect::onResize(int width, int height) {
    if (!renderer_) renderer_ = std::make_unique<growl::GrowlSharedRenderer>();
    renderer_->onResize(width, height);
}

std::uint32_t GrowlLabPreviewEffect::activeCount() const {
    return effect_.activeRingCount();
}

std::vector<std::string> GrowlLabPreviewEffect::overlayLines(
    const engine::tools::vfx_preview::PreviewSceneState& scene) const {
    (void)scene;
    return {};
}

void GrowlLabPreviewEffect::refreshManifestWriteTime() {
    std::error_code ec;
    if (!std::filesystem::exists(manifestPath_, ec) || ec) {
        manifestWriteTime_.reset();
        return;
    }
    manifestWriteTime_ = std::filesystem::last_write_time(manifestPath_, ec);
    if (ec) manifestWriteTime_.reset();
}

void GrowlLabPreviewEffect::pollManifestHotReload(
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
    std::cout << "[VfxLab] Detected Growl manifest change, hot reloaded preview\n";
}

} // namespace vfx::preview
