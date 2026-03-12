#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "engine/platform/Window.h"
#include "game/runtime/VideoPreferences.h"

struct SDL_Window;
class IRenderBackend;

namespace game::runtime::backend_bootstrap {

struct StartupBackendSelection {
    game::video::RendererBackend activeBackend = game::video::RendererBackend::OpenGL;
    bool fallback = false;
    std::string fallbackReason;
};

StartupBackendSelection selectStartupBackend(game::video::RendererBackend requestedBackend,
                                             std::string_view requestedBackendName);

Window::GraphicsApi graphicsApiForBackend(game::video::RendererBackend backend);

std::string makeBackendInitFallbackReason(std::string_view activeBackendName,
                                          std::string_view error);

std::unique_ptr<IRenderBackend> createRenderBackend(game::video::RendererBackend backend,
                                                    SDL_Window* sdlWindow,
                                                    int width,
                                                    int height,
                                                    bool vsyncEnabled,
                                                    const std::string& preferredAdapter,
                                                    std::string* outError);

} // namespace game::runtime::backend_bootstrap
