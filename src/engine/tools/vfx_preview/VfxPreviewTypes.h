#pragma once

#include <glm/glm.hpp>

class Camera3D;
class TextRenderer;

namespace engine::tools::vfx_preview {

struct PreviewSceneState {
    glm::vec3 emitter{0.0f, 0.42f, 0.0f};
    glm::vec3 target{0.0f, 0.35f, 4.2f};
    bool showEmitterMarker = true;
    bool showTargetMarker = true;
    bool showOrientationGuide = true;
    bool paused = false;
    bool loopPlayback = true;
    float timeScale = 1.0f;
};

struct PreviewFrameContext {
    const Camera3D& camera;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    TextRenderer* overlayText = nullptr;
};

} // namespace engine::tools::vfx_preview
