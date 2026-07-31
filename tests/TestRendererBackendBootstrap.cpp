#include <string>

#include "engine/platform/Window.h"
#include "game/runtime/renderer/RendererBackendBootstrap.h"

bool test_renderer_backend_bootstrap_policy(std::string& outFail) {
    using game::runtime::backend_bootstrap::graphicsApiForBackend;
    using game::runtime::backend_bootstrap::makeBackendInitFallbackReason;
    using game::runtime::backend_bootstrap::selectStartupBackend;
    using game::video::RendererBackend;

    {
        const auto selection = selectStartupBackend(RendererBackend::OpenGL, "opengl");
        if (selection.activeBackend != RendererBackend::OpenGL ||
            selection.fallback ||
            !selection.fallbackReason.empty()) {
            outFail = "OpenGL startup selection should be direct and non-fallback.";
            return false;
        }
    }

    {
        const auto selection = selectStartupBackend(RendererBackend::Auto, "auto");
#if defined(_WIN32)
        constexpr RendererBackend expectedAuto =
            RendererBackend::D3D12;
#elif defined(__linux__)
        constexpr RendererBackend expectedAuto =
            RendererBackend::Vulkan;
#else
        constexpr RendererBackend expectedAuto =
            RendererBackend::OpenGL;
#endif
        if (selection.activeBackend != expectedAuto ||
            selection.fallback ||
            !selection.fallbackReason.empty()) {
            outFail =
                "Auto startup selection did not choose the platform-native backend.";
            return false;
        }
    }

    {
        const auto selection = selectStartupBackend(RendererBackend::Vulkan, "vulkan");
        if (selection.activeBackend != RendererBackend::Vulkan ||
            selection.fallback ||
            !selection.fallbackReason.empty()) {
            outFail = "Vulkan startup selection should be direct and non-fallback.";
            return false;
        }
    }

    if (graphicsApiForBackend(RendererBackend::OpenGL) != Window::GraphicsApi::OpenGL) {
        outFail = "OpenGL graphics API mapping mismatch.";
        return false;
    }
    if (graphicsApiForBackend(RendererBackend::D3D12) != Window::GraphicsApi::Native) {
        outFail = "D3D12 graphics API mapping mismatch.";
        return false;
    }
    if (graphicsApiForBackend(RendererBackend::Vulkan) != Window::GraphicsApi::Vulkan) {
        outFail = "Vulkan graphics API mapping mismatch.";
        return false;
    }

    const std::string fallbackReason = makeBackendInitFallbackReason("d3d12", "mock error");
    if (fallbackReason.find("d3d12") == std::string::npos ||
        fallbackReason.find("mock error") == std::string::npos ||
        fallbackReason.find("OpenGL") == std::string::npos) {
        outFail = "Backend init fallback reason should include active backend, error, and fallback target.";
        return false;
    }

    return true;
}

