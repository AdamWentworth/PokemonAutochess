#include "game/runtime/renderer/RuntimeRendererRecovery.h"

#include "engine/render/IRenderBackend.h"
#include "game/runtime/renderer/RendererBackendBootstrap.h"

namespace game::runtime::renderer_recovery {

Result createWithFallbacks(const Inputs &inputs,
                           const BackendCreator &createBackend,
                           const FallbackWindowReinitializer &reopenWindow,
                           const OpenGlContextInitializer &initializeOpenGlContext,
                           const WindowStateSync &syncWindowState) {
    Result out;
    out.activeBackend = inputs.activeBackend;
    out.activeBackendName = inputs.activeBackendName;

    if (!game::video::isRendererBackendImplemented(inputs.activeBackend)) {
        out.failureStage = FailureStage::InitialBackendCreate;
        out.error = "Initial renderer is not enrolled for this project/build";
        return out;
    }
    std::string backendError;
    out.renderer = createBackend ? createBackend(inputs.activeBackend, &backendError) : nullptr;
    if (out.renderer) {
        return out;
    }

    out.failureStage = FailureStage::InitialBackendCreate;
    out.error = backendError;

    for (const auto backend : inputs.fallbackBackends) {
        if (backend == inputs.activeBackend || backend == game::video::RendererBackend::Auto ||
            !game::video::isRendererBackendImplemented(backend)) continue;
        out.rendererBackendFallback = true;
        out.rendererBackendFallbackReason = game::runtime::backend_bootstrap::makeBackendInitFallbackReason(
            inputs.activeBackendName, backendError, game::video::rendererBackendName(backend));
        const auto fallbackWindow = reopenWindow ? reopenWindow(backend) : FallbackWindowResult{};
        if (!fallbackWindow.success) {
            out.failureStage = FailureStage::FallbackWindowOpen;
            out.error = fallbackWindow.error;
            continue;
        }
        if (backend == game::video::RendererBackend::OpenGL) {
            std::string error;
            if (!initializeOpenGlContext || !initializeOpenGlContext(&error)) {
                out.failureStage = FailureStage::FallbackOpenGlInit;
                out.error = error;
                continue;
            }
        }
        if (syncWindowState) syncWindowState();
        out.activeBackend = backend;
        out.activeBackendName = game::video::rendererBackendName(backend);
        backendError.clear();
        out.renderer = createBackend ? createBackend(backend, &backendError) : nullptr;
        if (!out.renderer) {
            out.failureStage = FailureStage::FallbackBackendCreate;
            out.error = backendError;
            continue;
        }
        out.failureStage = FailureStage::None;
        out.error.clear();
        return out;
    }
    return out;
}

} // namespace game::runtime::renderer_recovery

