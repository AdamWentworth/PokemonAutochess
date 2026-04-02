#include "game/preview/effects/GrowlPreviewEffect.h"

#include "vfx/preview/growl/GrowlPreviewController.h"

namespace game::preview {

GrowlPreviewEffect::GrowlPreviewEffect()
    : controller_(std::make_unique<vfx::preview::growl::GrowlPreviewController>(
          "[VfxPreviewer]")) {}

GrowlPreviewEffect::~GrowlPreviewEffect() = default;

std::string_view GrowlPreviewEffect::name() const {
    return "Growl";
}

void GrowlPreviewEffect::onActivated(engine::tools::vfx_preview::PreviewSceneState& scene) {
    controller_->onActivated(scene);
}

void GrowlPreviewEffect::replay(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    controller_->replay(scene);
}

void GrowlPreviewEffect::reload(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    controller_->reload(scene);
}

void GrowlPreviewEffect::update(float dt, const engine::tools::vfx_preview::PreviewSceneState& scene) {
    controller_->update(dt, scene);
}

void GrowlPreviewEffect::stepFrames(int frames, const engine::tools::vfx_preview::PreviewSceneState& scene) {
    (void)scene;
    controller_->stepFrames(frames);
}

void GrowlPreviewEffect::render(const engine::tools::vfx_preview::PreviewFrameContext& frame) {
    controller_->render(frame);
}

void GrowlPreviewEffect::onResize(int width, int height) {
    controller_->onResize(width, height);
}

std::uint32_t GrowlPreviewEffect::activeCount() const {
    return controller_->activeCount();
}

engine::tools::vfx_preview::PreviewCasterAnimationRequest
GrowlPreviewEffect::casterAnimationRequest() const {
    return {
        .kind = "charged",
        .move = "growl",
        .phase = "one_shot",
    };
}

engine::tools::vfx_preview::PreviewPokemonSpeciesSelection
GrowlPreviewEffect::previewPokemonSpecies() const {
    return {
        .attackerSpecies = "charmander",
        .targetSpecies = "bulbasaur",
    };
}

std::vector<std::string> GrowlPreviewEffect::overlayLines(
    const engine::tools::vfx_preview::PreviewSceneState& scene) const {
    (void)scene;
    return {
        "Growl uses the shared/backend batch path and hot reloads its draw-pass manifest."
    };
}

} // namespace game::preview
