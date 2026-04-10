#include "vfx/preview/VfxLibraryPreviewProject.h"

#include <stdexcept>

#include "vfx/preview/effects/GrowlLabPreviewEffect.h"
#include "vfx/preview/effects/ScratchLabPreviewEffect.h"
#include "vfx/preview/effects/TackleLabPreviewEffect.h"

namespace vfx::preview {

VfxLibraryPreviewProject::VfxLibraryPreviewProject() {
    effects_.push_back(std::make_unique<GrowlLabPreviewEffect>());
    effects_.push_back(std::make_unique<ScratchLabPreviewEffect>());
    effects_.push_back(std::make_unique<TackleLabPreviewEffect>());
}

VfxLibraryPreviewProject::~VfxLibraryPreviewProject() = default;

std::string_view VfxLibraryPreviewProject::projectName() const {
    return "VfxLab";
}

std::size_t VfxLibraryPreviewProject::effectCount() const {
    return effects_.size();
}

engine::tools::vfx_preview::IVfxPreviewEffect&
VfxLibraryPreviewProject::effectAt(std::size_t index) {
    if (index >= effects_.size()) throw std::out_of_range("Invalid VFX lab effect index");
    return *effects_[index];
}

const engine::tools::vfx_preview::IVfxPreviewEffect&
VfxLibraryPreviewProject::effectAt(std::size_t index) const {
    if (index >= effects_.size()) throw std::out_of_range("Invalid VFX lab effect index");
    return *effects_[index];
}

std::size_t VfxLibraryPreviewProject::rigCount() const {
    return 1u;
}

std::string_view VfxLibraryPreviewProject::rigName(std::size_t index) const {
    (void)index;
    return "Free Scene";
}

bool VfxLibraryPreviewProject::defaultPrimaryBackdropEnabled(std::size_t rigIndex) const {
    (void)rigIndex;
    return false;
}

bool VfxLibraryPreviewProject::defaultSecondaryBackdropEnabled(std::size_t rigIndex) const {
    (void)rigIndex;
    return false;
}

bool VfxLibraryPreviewProject::supportsPrimaryBackdropToggle(std::size_t rigIndex) const {
    (void)rigIndex;
    return false;
}

bool VfxLibraryPreviewProject::supportsSecondaryBackdropToggle(std::size_t rigIndex) const {
    (void)rigIndex;
    return false;
}

void VfxLibraryPreviewProject::applyRigDefaults(
    std::size_t rigIndex,
    engine::tools::vfx_preview::PreviewSceneState& scene) const {
    (void)rigIndex;
    scene.emitter = glm::vec3(0.0f, 0.42f, 0.0f);
    scene.target = glm::vec3(0.0f, 0.35f, 4.2f);
}

void VfxLibraryPreviewProject::constrainScene(
    std::size_t rigIndex,
    engine::tools::vfx_preview::PreviewSceneState& scene) const {
    (void)rigIndex;
    scene.target.y = 0.35f;
}

void VfxLibraryPreviewProject::renderBackdrop(
    const engine::tools::vfx_preview::PreviewFrameContext& frame,
    std::size_t rigIndex,
    const engine::tools::vfx_preview::PreviewSceneState& scene,
    bool primaryBackdropEnabled,
    bool secondaryBackdropEnabled) {
    (void)frame;
    (void)rigIndex;
    (void)scene;
    (void)primaryBackdropEnabled;
    (void)secondaryBackdropEnabled;
}

void VfxLibraryPreviewProject::appendDebugMarkers(
    engine::tools::vfx_preview::IPreviewDebugDraw& draw,
    const engine::tools::vfx_preview::PreviewSceneState& scene) const {
    const glm::vec3 emitterColor(1.0f, 0.52f, 0.16f);
    const glm::vec3 targetColor(0.28f, 0.95f, 0.55f);
    const glm::vec3 guideColor(0.95f, 0.90f, 0.35f);

    if (scene.showEmitterMarker) {
        draw.addCross(scene.emitter, 0.16f, emitterColor);
        draw.addCircleXZ(glm::vec3(scene.emitter.x, 0.015f, scene.emitter.z), 0.20f, emitterColor, 28);
    }

    if (scene.showTargetMarker) {
        draw.addCross(scene.target, 0.18f, targetColor);
        draw.addCircleXZ(glm::vec3(scene.target.x, 0.015f, scene.target.z), 0.24f, targetColor, 28);
    }

    if (scene.showOrientationGuide) {
        const glm::vec3 guideStart = scene.emitter + glm::vec3(0.0f, 0.02f, 0.0f);
        const glm::vec3 guideEnd = glm::vec3(scene.target.x, guideStart.y, scene.target.z);
        draw.addArrow(guideStart, guideEnd, guideColor);
    }
}

std::vector<std::string> VfxLibraryPreviewProject::overlayLines(
    const engine::tools::vfx_preview::PreviewSceneState& scene,
    std::size_t rigIndex) const {
    (void)scene;
    (void)rigIndex;
    return {};
}

} // namespace vfx::preview
