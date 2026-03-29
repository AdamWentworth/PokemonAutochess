#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "engine/tools/vfx_preview/PreviewDebugDraw.h"
#include "engine/tools/vfx_preview/VfxPreviewTypes.h"

namespace engine::tools::vfx_preview {

class IVfxPreviewEffect;

class IVfxPreviewProject {
public:
    virtual ~IVfxPreviewProject() = default;

    virtual std::string_view projectName() const = 0;
    virtual std::size_t effectCount() const = 0;
    virtual IVfxPreviewEffect& effectAt(std::size_t index) = 0;
    virtual const IVfxPreviewEffect& effectAt(std::size_t index) const = 0;

    virtual std::size_t rigCount() const { return 1u; }
    virtual std::string_view rigName(std::size_t index) const {
        (void)index;
        return "Default";
    }
    virtual bool defaultPrimaryBackdropEnabled(std::size_t rigIndex) const {
        (void)rigIndex;
        return true;
    }
    virtual bool defaultSecondaryBackdropEnabled(std::size_t rigIndex) const {
        (void)rigIndex;
        return false;
    }
    virtual void onEffectActivated(std::size_t effectIndex) {
        (void)effectIndex;
    }
    virtual void applyRigDefaults(std::size_t rigIndex, PreviewSceneState& scene) const {
        (void)rigIndex;
        (void)scene;
    }
    virtual void constrainScene(std::size_t rigIndex, PreviewSceneState& scene) const {
        (void)rigIndex;
        (void)scene;
    }
    virtual void update(float dt, std::size_t rigIndex, const PreviewSceneState& scene) {
        (void)dt;
        (void)rigIndex;
        (void)scene;
    }

    virtual void renderBackdrop(const PreviewFrameContext& frame,
                                std::size_t rigIndex,
                                const PreviewSceneState& scene,
                                bool primaryBackdropEnabled,
                                bool secondaryBackdropEnabled) = 0;
    virtual void appendDebugMarkers(IPreviewDebugDraw& draw, const PreviewSceneState& scene) const = 0;

    virtual std::vector<std::string> overlayLines(const PreviewSceneState& scene,
                                                  std::size_t rigIndex) const {
        (void)scene;
        (void)rigIndex;
        return {};
    }
};

} // namespace engine::tools::vfx_preview
