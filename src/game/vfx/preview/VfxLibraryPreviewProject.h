#pragma once

#include <memory>
#include <vector>

#include "engine/tools/vfx_preview/IVfxPreviewProject.h"

namespace vfx::preview {

class VfxLibraryPreviewProject final : public engine::tools::vfx_preview::IVfxPreviewProject {
public:
    VfxLibraryPreviewProject();
    ~VfxLibraryPreviewProject() override;

    std::string_view projectName() const override;
    std::size_t effectCount() const override;
    engine::tools::vfx_preview::IVfxPreviewEffect& effectAt(std::size_t index) override;
    const engine::tools::vfx_preview::IVfxPreviewEffect& effectAt(std::size_t index) const override;

    std::size_t rigCount() const override;
    std::string_view rigName(std::size_t index) const override;
    bool defaultPrimaryBackdropEnabled(std::size_t rigIndex) const override;
    bool defaultSecondaryBackdropEnabled(std::size_t rigIndex) const override;
    bool supportsPrimaryBackdropToggle(std::size_t rigIndex) const override;
    bool supportsSecondaryBackdropToggle(std::size_t rigIndex) const override;
    void applyRigDefaults(std::size_t rigIndex,
                          engine::tools::vfx_preview::PreviewSceneState& scene) const override;
    void constrainScene(std::size_t rigIndex,
                        engine::tools::vfx_preview::PreviewSceneState& scene) const override;
    void renderBackdrop(const engine::tools::vfx_preview::PreviewFrameContext& frame,
                        std::size_t rigIndex,
                        const engine::tools::vfx_preview::PreviewSceneState& scene,
                        bool primaryBackdropEnabled,
                        bool secondaryBackdropEnabled) override;
    void appendDebugMarkers(engine::tools::vfx_preview::IPreviewDebugDraw& draw,
                            const engine::tools::vfx_preview::PreviewSceneState& scene) const override;
    std::vector<std::string> overlayLines(
        const engine::tools::vfx_preview::PreviewSceneState& scene,
        std::size_t rigIndex) const override;

private:
    std::vector<std::unique_ptr<engine::tools::vfx_preview::IVfxPreviewEffect>> effects_;
};

} // namespace vfx::preview
