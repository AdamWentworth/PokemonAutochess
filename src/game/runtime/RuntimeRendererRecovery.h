#pragma once

#include "game/runtime/VideoPreferences.h"

#include <functional>
#include <memory>
#include <string>

class IRenderBackend;

namespace game::runtime::renderer_recovery {

enum class FailureStage {
    None,
    InitialBackendCreate,
    FallbackWindowOpen,
    FallbackOpenGlInit,
    FallbackBackendCreate
};

struct Inputs {
    game::video::RendererBackend activeBackend = game::video::RendererBackend::OpenGL;
    std::string activeBackendName = "opengl";
};

struct OpenGlWindowResult {
    bool success = false;
    std::string error;
};

struct Result {
    std::unique_ptr<IRenderBackend> renderer;
    game::video::RendererBackend activeBackend = game::video::RendererBackend::OpenGL;
    std::string activeBackendName = "opengl";
    bool rendererBackendFallback = false;
    std::string rendererBackendFallbackReason;
    FailureStage failureStage = FailureStage::None;
    std::string error;
};

using BackendCreator = std::function<std::unique_ptr<IRenderBackend>(game::video::RendererBackend, std::string*)>;
using OpenGlWindowReinitializer = std::function<OpenGlWindowResult()>;
using OpenGlContextInitializer = std::function<bool(std::string*)>;
using WindowStateSync = std::function<void()>;

Result createWithOpenGlFallback(const Inputs& inputs,
                                const BackendCreator& createBackend,
                                const OpenGlWindowReinitializer& reopenOpenGlWindow,
                                const OpenGlContextInitializer& initializeOpenGlContext,
                                const WindowStateSync& syncWindowState);

} // namespace game::runtime::renderer_recovery
