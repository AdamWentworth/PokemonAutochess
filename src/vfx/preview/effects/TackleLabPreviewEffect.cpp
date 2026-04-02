#include "vfx/preview/effects/TackleLabPreviewEffect.h"

#include "vfx/preview/tackle/TacklePreviewController.h"

namespace vfx::preview {

TackleLabPreviewEffect::TackleLabPreviewEffect()
    : controller_(std::make_unique<tackle::TacklePreviewController>("[VfxLab]")) {}

TackleLabPreviewEffect::~TackleLabPreviewEffect() = default;

std::string_view TackleLabPreviewEffect::name() const {
    return "Tackle";
}

void TackleLabPreviewEffect::onActivated(engine::tools::vfx_preview::PreviewSceneState& scene) {
    controller_->onActivated(scene);
}

void TackleLabPreviewEffect::replay(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    controller_->replay(scene);
}

void TackleLabPreviewEffect::reload(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    controller_->reload(scene);
}

void TackleLabPreviewEffect::update(float dt, const engine::tools::vfx_preview::PreviewSceneState& scene) {
    controller_->update(dt, scene);
}

void TackleLabPreviewEffect::stepFrames(
    int frames,
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    (void)scene;
    controller_->stepFrames(frames);
}

void TackleLabPreviewEffect::render(const engine::tools::vfx_preview::PreviewFrameContext& frame) {
    controller_->render(frame);
}

void TackleLabPreviewEffect::onResize(int width, int height) {
    controller_->onResize(width, height);
}

std::uint32_t TackleLabPreviewEffect::activeCount() const {
    return controller_->activeCount();
}

std::vector<std::string> TackleLabPreviewEffect::overlayLines(
    const engine::tools::vfx_preview::PreviewSceneState& scene) const {
    (void)scene;
    return {};
}

} // namespace vfx::preview
