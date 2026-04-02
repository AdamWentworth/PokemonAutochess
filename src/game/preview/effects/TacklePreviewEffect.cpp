#include "game/preview/effects/TacklePreviewEffect.h"

#include "vfx/preview/tackle/TacklePreviewController.h"

namespace game::preview {

TacklePreviewEffect::TacklePreviewEffect()
    : controller_(std::make_unique<vfx::preview::tackle::TacklePreviewController>(
          "[VfxPreviewer]")) {}

TacklePreviewEffect::~TacklePreviewEffect() = default;

std::string_view TacklePreviewEffect::name() const {
    return "Tackle";
}

void TacklePreviewEffect::onActivated(engine::tools::vfx_preview::PreviewSceneState& scene) {
    controller_->onActivated(scene);
    scene.showOrientationGuide = false;
}

void TacklePreviewEffect::replay(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    controller_->replay(scene);
}

void TacklePreviewEffect::reload(const engine::tools::vfx_preview::PreviewSceneState& scene) {
    controller_->reload(scene);
}

void TacklePreviewEffect::update(float dt, const engine::tools::vfx_preview::PreviewSceneState& scene) {
    controller_->update(dt, scene);
}

void TacklePreviewEffect::stepFrames(
    int frames,
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    (void)scene;
    controller_->stepFrames(frames);
}

void TacklePreviewEffect::render(const engine::tools::vfx_preview::PreviewFrameContext& frame) {
    controller_->render(frame);
}

void TacklePreviewEffect::onResize(int width, int height) {
    controller_->onResize(width, height);
}

std::uint32_t TacklePreviewEffect::activeCount() const {
    return controller_->activeCount();
}

engine::tools::vfx_preview::PreviewCasterAnimationRequest
TacklePreviewEffect::casterAnimationRequest() const {
    return {
        .kind = "fast",
        .move = "tackle",
        .phase = "one_shot",
    };
}

engine::tools::vfx_preview::PreviewPokemonSpeciesSelection
TacklePreviewEffect::previewPokemonSpecies() const {
    return {
        .attackerSpecies = "bulbasaur",
        .targetSpecies = "charmander",
    };
}

bool TacklePreviewEffect::wantsExactClipMotionPreview() const {
    return true;
}

bool TacklePreviewEffect::wantsTargetSurfaceImpactPoint() const {
    return true;
}

std::vector<std::string> TacklePreviewEffect::overlayLines(
    const engine::tools::vfx_preview::PreviewSceneState& scene) const {
    (void)scene;
    return {
        "Tackle uses the shared authored-batch path with alpha-blended smoke billboards."
    };
}

} // namespace game::preview
