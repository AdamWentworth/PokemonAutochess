#pragma once

#include "game/runtime/video/VideoPreferences.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

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
    game::video::RendererBackend activeBackend = game::video::RendererBackend::Auto;
    std::string activeBackendName = "auto";
    std::vector<game::video::RendererBackend> fallbackBackends;
};

struct FallbackWindowResult {
    bool success = false;
    std::string error;
};

struct Result {
    std::unique_ptr<IRenderBackend> renderer;
    game::video::RendererBackend activeBackend = game::video::RendererBackend::Auto;
    std::string activeBackendName = "auto";
    bool rendererBackendFallback = false;
    std::string rendererBackendFallbackReason;
    FailureStage failureStage = FailureStage::None;
    std::string error;
};

using BackendCreator = std::function<std::unique_ptr<IRenderBackend>(game::video::RendererBackend, std::string*)>;
using FallbackWindowReinitializer = std::function<FallbackWindowResult(game::video::RendererBackend)>;
using OpenGlContextInitializer = std::function<bool(std::string*)>;
using WindowStateSync = std::function<void()>;

Result createWithFallbacks(const Inputs& inputs,
                                const BackendCreator& createBackend,
                                const FallbackWindowReinitializer& reopenWindow,
                                const OpenGlContextInitializer& initializeOpenGlContext,
                                const WindowStateSync& syncWindowState);

} // namespace game::runtime::renderer_recovery

