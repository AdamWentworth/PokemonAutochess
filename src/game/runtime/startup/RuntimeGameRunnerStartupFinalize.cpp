#include "game/runtime/startup/RuntimeGameRunnerStartupFinalize.h"

#include "engine/core/EngineServices.h"
#include "engine/render/Camera3D.h"
#include "engine/render/IRenderBackend.h"
#include "game/runtime/renderer/RuntimeRendererStartupState.h"
#include "game/runtime/startup/RuntimeStartupPresentation.h"
#include "game/runtime/video/RuntimeWindowPresentationController.h"

#include <SDL2/SDL_ttf.h>
#include <glad/glad.h>

#include <ostream>

namespace {

const char* glStringOrUnknown(GLenum token) {
    const GLubyte* s = glGetString(token);
    return s ? reinterpret_cast<const char*>(s) : "<unknown>";
}

} // namespace

namespace game::runtime::runner_startup_finalize {

Result activateRendererAndInitializePresentation(
    IRenderBackend& renderer,
    EngineServices& services,
    game::runtime::window_presentation::WindowPresentationController& presentation,
    std::unique_ptr<Camera3D>& camera,
    const std::function<void(float)>& renderBootLoading,
    std::ostream& logOut,
    std::ostream& errOut) {
    Result out;

    game::runtime::renderer_startup_state::OpenGlStrings openGlStrings;
    if (renderer.requiresOpenGLContext()) {
        openGlStrings.vendor = glStringOrUnknown(GL_VENDOR);
        openGlStrings.renderer = glStringOrUnknown(GL_RENDERER);
        openGlStrings.version = glStringOrUnknown(GL_VERSION);
        openGlStrings.glslVersion = glStringOrUnknown(GL_SHADING_LANGUAGE_VERSION);
    }
    const auto activationInputs =
        game::runtime::renderer_startup_state::makeActivationInputs(
            services,
            renderer,
            openGlStrings);
    const auto activation =
        game::runtime::renderer_startup_state::applyAndLog(services, activationInputs, logOut);
    if (!activation.discreteRequirementSatisfied) {
        out.error =
            "[GPU] Discrete GPU required by settings, but integrated GPU is active.\n"
            "[GPU] Change Graphics preference to high performance or choose a discrete adapter.\n";
        return out;
    }

    const auto fontInit = game::runtime::startup_presentation::initializeFonts(
        []() { return TTF_Init(); },
        []() { return std::string(TTF_GetError()); });
    if (!fontInit.succeeded) {
        errOut << "[GameRunner] TTF_Init error: " << fontInit.error << "\n";
    }

    camera = game::runtime::startup_presentation::createDefaultCamera(
        presentation.drawableWidth(),
        presentation.drawableHeight());
    presentation.bindCamera(camera.get());
    game::runtime::startup_presentation::primeInitialLoadingFrame(
        &renderer,
        presentation.drawableWidth(),
        presentation.drawableHeight(),
        renderBootLoading);

    out.success = true;
    return out;
}

} // namespace game::runtime::runner_startup_finalize
