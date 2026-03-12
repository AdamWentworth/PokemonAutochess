#include "game/runtime/RuntimeStartupPresentation.h"

#include "engine/render/IRenderBackend.h"

namespace game::runtime::startup_presentation {

FontInitResult initializeFonts(const std::function<int()>& initFonts,
                               const std::function<std::string()>& readError) {
    FontInitResult out;
    if (!initFonts) {
        return out;
    }
    if (initFonts() != -1) {
        return out;
    }
    out.succeeded = false;
    out.error = readError ? readError() : std::string();
    return out;
}

std::unique_ptr<Camera3D> createDefaultCamera(int drawableW, int drawableH) {
    const int safeDrawableH = drawableH > 0 ? drawableH : 1;
    return std::make_unique<Camera3D>(
        45.0f,
        static_cast<float>(drawableW) / static_cast<float>(safeDrawableH),
        0.1f,
        100.0f);
}

bool primeInitialLoadingFrame(IRenderBackend* renderer,
                              int drawableW,
                              int drawableH,
                              const std::function<void(float)>& renderBootLoading) {
    if (!renderer) {
        return false;
    }
    renderer->onResize(drawableW, drawableH);
    if (renderBootLoading) {
        renderBootLoading(0.0f);
    }
    return true;
}

} // namespace game::runtime::startup_presentation
