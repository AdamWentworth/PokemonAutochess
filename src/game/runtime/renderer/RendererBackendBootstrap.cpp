#include "ProjectRendererPolicy.h"
#include "engine/render/LegacyOpenGL.h"
#include "game/runtime/renderer/RendererBackendBootstrap.h"

#include <exception>
#include "engine/core/Paths.h"
#include <memory>

#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/IRenderBackend.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/VulkanRenderBackend.h"

namespace game::runtime::backend_bootstrap {

StartupBackendSelection selectStartupBackend(game::video::RendererBackend requestedBackend,
                                             std::string_view requestedBackendName) {
    (void)requestedBackendName;
    StartupBackendSelection out;
    const auto candidates = engine::render::rendererCandidates(projectRendererPolicy(), game::video::rendererBackendName(requestedBackend));
    out.activeBackend = game::video::parseRendererBackend(candidates.front());
    return out;
}

Window::GraphicsApi graphicsApiForBackend(game::video::RendererBackend backend) {
    switch (backend) {
    case game::video::RendererBackend::Auto:
        return graphicsApiForBackend(selectStartupBackend(backend, "auto").activeBackend);
    case game::video::RendererBackend::OpenGL:
        return Window::GraphicsApi::OpenGL;
    case game::video::RendererBackend::D3D12:
        return Window::GraphicsApi::Native;
    case game::video::RendererBackend::Vulkan:
        return Window::GraphicsApi::Vulkan;
    default:
        return Window::GraphicsApi::OpenGL;
    }
}

std::string makeBackendInitFallbackReason(std::string_view activeBackendName,
                                          std::string_view error, std::string_view fallbackBackend) {
    return "backend '" + std::string(activeBackendName) + "' failed to initialize (" +
        std::string(error) + "); trying enrolled renderer " + std::string(fallbackBackend) + ".";
}

std::unique_ptr<IRenderBackend> createRenderBackend(game::video::RendererBackend backend,
                                                    SDL_Window* sdlWindow,
                                                    int width,
                                                    int height,
                                                    bool vsyncEnabled,
                                                    const std::string& preferredAdapter,
                                                    std::string* outError) {
    try {
        backend = selectStartupBackend(backend, game::video::rendererBackendName(backend)).activeBackend;
        const auto profile = engine::render::loadWorldMaterialProfile(
            engine::paths::data(""), "config/render/world_materials.json");
        switch (backend) {
        case game::video::RendererBackend::Auto:
        case game::video::RendererBackend::OpenGL:
            return engine::render::createLegacyOpenGLBackend(profile);
        case game::video::RendererBackend::D3D12:
            return std::make_unique<D3D12RenderBackend>(
                sdlWindow,
                width,
                height,
                vsyncEnabled,
                preferredAdapter, profile);
        case game::video::RendererBackend::Vulkan:
            return std::make_unique<VulkanRenderBackend>(
                sdlWindow,
                width,
                height,
                vsyncEnabled,
                preferredAdapter, profile);
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

