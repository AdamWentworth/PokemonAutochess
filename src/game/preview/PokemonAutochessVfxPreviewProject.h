#pragma once

#include <memory>
#include <vector>

#include "engine/tools/vfx_preview/IVfxPreviewProject.h"

class BoardRenderer;

namespace game::preview {

class PokemonAutochessVfxPreviewProject final : public engine::tools::vfx_preview::IVfxPreviewProject {
public:
    PokemonAutochessVfxPreviewProject();
    ~PokemonAutochessVfxPreviewProject() override;

    std::string_view projectName() const override;
    std::size_t effectCount() const override;
    engine::tools::vfx_preview::IVfxPreviewEffect& effectAt(std::size_t index) override;
    const engine::tools::vfx_preview::IVfxPreviewEffect& effectAt(std::size_t index) const override;

    std::size_t rigCount() const override;
    std::string_view rigName(std::size_t index) const override;
    bool defaultPrimaryBackdropEnabled(std::size_t rigIndex) const override;
    bool defaultSecondaryBackdropEnabled(std::size_t rigIndex) const override;
    void onEffectActivated(std::size_t effectIndex) override;
    void applyRigDefaults(std::size_t rigIndex,
                          engine::tools::vfx_preview::PreviewSceneState& scene) const override;
    void constrainScene(std::size_t rigIndex,
                        engine::tools::vfx_preview::PreviewSceneState& scene) const override;
    void update(float dt,
                std::size_t rigIndex,
                const engine::tools::vfx_preview::PreviewSceneState& scene) override;

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
    enum class RigKind : std::size_t {
        FreeScene = 0,
        AdjacentBoard = 1,
        PokemonModels = 2,
    };

    struct Impl;
    std::unique_ptr<BoardRenderer> board_;
    std::vector<std::unique_ptr<engine::tools::vfx_preview::IVfxPreviewEffect>> effects_;
    std::unique_ptr<Impl> impl_;
};

} // namespace game::preview
