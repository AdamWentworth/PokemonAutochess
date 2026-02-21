#pragma once

namespace game::runtime::render {

inline bool shouldRenderBackendDebugLayer(bool renderEnabled,
                                          bool legacyRenderPath,
                                          bool renderWorldRequested) {
    return renderEnabled && !legacyRenderPath && renderWorldRequested;
}

inline bool shouldRenderBackendWorldBackdrop(bool renderWorldRequested,
                                             bool legacyRenderPath,
                                             bool allowMenuBackdrop) {
    if (renderWorldRequested) return true;
    if (legacyRenderPath) return false;
    return allowMenuBackdrop;
}

} // namespace game::runtime::render
