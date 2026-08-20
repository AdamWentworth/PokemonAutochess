#pragma once

namespace game::preview {

struct PreviewBodyRenderRouting {
    bool buildProjectedScratch = false;
    bool allowProjectedBody = false;
};

inline PreviewBodyRenderRouting resolvePreviewBodyRenderRouting(
    bool exactClipMotionPreview) {
    PreviewBodyRenderRouting routing{};
    routing.allowProjectedBody = !exactClipMotionPreview;
    routing.buildProjectedScratch = routing.allowProjectedBody;
    return routing;
}

} // namespace game::preview
