#include "game/runtime/RuntimeRendererRecovery.h"

#include "engine/render/IRenderBackend.h"
#include "game/runtime/RendererBackendBootstrap.h"

namespace game::runtime::renderer_recovery {

Result createWithOpenGlFallback(const Inputs& inputs,
                                const BackendCreator& createBackend,
                                const OpenGlWindowReinitializer& reopenOpenGlWindow,
                                const OpenGlContextInitializer& initializeOpenGlContext,
                                const WindowStateSync& syncWindowState) {
    Result out;
    out.activeBackend = inputs.activeBackend;
    out.activeBackendName = inputs.activeBackendName;

    std::string backendError;
    out.renderer = createBackend ? createBackend(inputs.activeBackend, &backendError) : nullptr;
    if (out.renderer) {
        return out;
    }

    out.failureStage = FailureStage::InitialBackendCreate;
    out.error = backendError;
    if (inputs.activeBackend == game::video::RendererBackend::OpenGL) {
        return out;
    }

    out.rendererBackendFallback = true;
    out.rendererBackendFallbackReason =
        game::runtime::backend_bootstrap::makeBackendInitFallbackReason(
            inputs.activeBackendName,
            backendError);

    const auto fallbackWindow = reopenOpenGlWindow ? reopenOpenGlWindow() : OpenGlWindowResult{};
    if (!fallbackWindow.success) {
        out.failureStage = FailureStage::FallbackWindowOpen;
        out.error = fallbackWindow.error;
        return out;
    }

    std::string openGlInitError;
    if (!initializeOpenGlContext || !initializeOpenGlContext(&openGlInitError)) {
        out.failureStage = FailureStage::FallbackOpenGlInit;
        out.error = openGlInitError;
        return out;
    }

    if (syncWindowState) {
        syncWindowState();
    }

    out.activeBackend = game::video::RendererBackend::OpenGL;
    out.activeBackendName = game::video::rendererBackendName(out.activeBackend);
    backendError.clear();
    out.renderer = createBackend ? createBackend(out.activeBackend, &backendError) : nullptr;
    if (!out.renderer) {
        out.failureStage = FailureStage::FallbackBackendCreate;
        out.error = backendError;
        return out;
    }

    out.failureStage = FailureStage::None;
    out.error.clear();
    return out;
}

} // namespace game::runtime::renderer_recovery
