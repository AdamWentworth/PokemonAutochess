#include "vfx/preview/effects/GrowlLabPreviewEffect.h"

#include "vfx/preview/growl/GrowlPreviewController.h"

namespace vfx::preview {

GrowlLabPreviewEffect::GrowlLabPreviewEffect()
    : controller_(std::make_unique<growl::GrowlPreviewController>("[VfxLab]")) {}

GrowlLabPreviewEffect::~GrowlLabPreviewEffect() = default;

std::string_view GrowlLabPreviewEffect::name() const {
    return "Growl";
}

void GrowlLabPreviewEffect::onActivated(engine::tools::vfx_preview::PreviewSceneState& scene) {
    controller_->onActivated(scene);
}

void GrowlLabPreviewEffect::replay(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    controller_->replay(scene);
}

void GrowlLabPreviewEffect::reload(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    controller_->reload(scene);
}

void GrowlLabPreviewEffect::update(float dt, const engine::tools::vfx_preview::PreviewSceneState& scene) {
    controller_->update(dt, scene);
}

void GrowlLabPreviewEffect::stepFrames(
    int frames,
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    (void)scene;
    controller_->stepFrames(frames);
}

void GrowlLabPreviewEffect::render(const engine::tools::vfx_preview::PreviewFrameContext& frame) {
    controller_->render(frame);
}

void GrowlLabPreviewEffect::onResize(int width, int height) {
    controller_->onResize(width, height);
}

std::uint32_t GrowlLabPreviewEffect::activeCount() const {
    return controller_->activeCount();
}

std::vector<std::string> GrowlLabPreviewEffect::overlayLines(
    const engine::tools::vfx_preview::PreviewSceneState& scene) const {
    (void)scene;
    return {};
}

} // namespace vfx::preview
