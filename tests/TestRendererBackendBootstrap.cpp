#include <string>

#include "engine/platform/Window.h"
#include "game/runtime/RendererBackendBootstrap.h"

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
        if (selection.activeBackend != RendererBackend::OpenGL ||
            selection.fallback ||
            !selection.fallbackReason.empty()) {
            outFail = "Auto startup selection should default to OpenGL without fallback.";
            return false;
        }
    }

    {
        const auto selection = selectStartupBackend(RendererBackend::Vulkan, "vulkan");
        if (selection.activeBackend != RendererBackend::OpenGL || !selection.fallback) {
            outFail = "Vulkan startup selection should currently fall back to OpenGL.";
            return false;
        }
        if (selection.fallbackReason.find("vulkan") == std::string::npos ||
            selection.fallbackReason.find("OpenGL") == std::string::npos) {
            outFail = "Vulkan fallback reason should mention both requested backend and OpenGL.";
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
    if (graphicsApiForBackend(RendererBackend::Vulkan) != Window::GraphicsApi::Native) {
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
