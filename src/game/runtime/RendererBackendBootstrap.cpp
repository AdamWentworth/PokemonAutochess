#include "game/runtime/RendererBackendBootstrap.h"

#include <exception>
#include <memory>

#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/IRenderBackend.h"
#include "engine/render/OpenGLRenderBackend.h"

namespace game::runtime::backend_bootstrap {

StartupBackendSelection selectStartupBackend(game::video::RendererBackend requestedBackend,
                                             std::string_view requestedBackendName) {
    StartupBackendSelection out;
    if (!game::video::isRendererBackendImplemented(requestedBackend)) {
        out.activeBackend = game::video::RendererBackend::OpenGL;
        out.fallback = true;
        out.fallbackReason =
            std::string("requested backend '") + std::string(requestedBackendName) +
            "' is not implemented; falling back to OpenGL.";
        return out;
    }

    if (requestedBackend == game::video::RendererBackend::Auto) {
        out.activeBackend = game::video::RendererBackend::OpenGL;
        return out;
    }

    out.activeBackend = requestedBackend;
    return out;
}

Window::GraphicsApi graphicsApiForBackend(game::video::RendererBackend backend) {
    switch (backend) {
    case game::video::RendererBackend::Auto:
    case game::video::RendererBackend::OpenGL:
        return Window::GraphicsApi::OpenGL;
    case game::video::RendererBackend::D3D12:
    case game::video::RendererBackend::Vulkan:
        return Window::GraphicsApi::Native;
    default:
        return Window::GraphicsApi::OpenGL;
    }
}

std::string makeBackendInitFallbackReason(std::string_view activeBackendName,
                                          std::string_view error) {
    return "backend '" + std::string(activeBackendName) + "' failed to initialize (" +
        std::string(error) + "); falling back to OpenGL.";
}

std::unique_ptr<IRenderBackend> createRenderBackend(game::video::RendererBackend backend,
                                                    SDL_Window* sdlWindow,
                                                    int width,
                                                    int height,
                                                    bool vsyncEnabled,
                                                    const std::string& preferredAdapter,
                                                    std::string* outError) {
    try {
        switch (backend) {
        case game::video::RendererBackend::Auto:
        case game::video::RendererBackend::OpenGL:
            return std::make_unique<OpenGLRenderBackend>();
        case game::video::RendererBackend::D3D12:
            return std::make_unique<D3D12RenderBackend>(
                sdlWindow,
                width,
                height,
                vsyncEnabled,
                preferredAdapter);
        case game::video::RendererBackend::Vulkan:
            if (outError) *outError = "Vulkan backend is not implemented.";
            return nullptr;
        default:
            if (outError) *outError = "Unknown renderer backend.";
            return nullptr;
        }
    } catch (const std::exception& e) {
        if (outError) *outError = e.what();
        return nullptr;
    }
}

} // namespace game::runtime::backend_bootstrap
