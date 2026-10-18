#include <stdexcept>
#include <string>

#include "engine/platform/Window.h"
#include "game/runtime/renderer/RendererBackendBootstrap.h"

bool test_renderer_backend_bootstrap_policy(std::string& outFail) {
    using game::runtime::backend_bootstrap::graphicsApiForBackend;
    using game::runtime::backend_bootstrap::makeBackendInitFallbackReason;
    using game::runtime::backend_bootstrap::selectStartupBackend;
    using game::video::RendererBackend;

    {
        bool rejected = false;
        try {
            selectStartupBackend(RendererBackend::OpenGL, "opengl");
        } catch (const std::exception &) {
            rejected = true;
        }
        if (!rejected || game::video::isRendererBackendImplemented(RendererBackend::OpenGL)) {
            outFail = "OpenGL must stay unenrolled even in an optional legacy engine build.";
            return false;
        }
    }

    {
        const auto selection = selectStartupBackend(RendererBackend::Auto, "auto");
        constexpr RendererBackend expectedAuto = RendererBackend::Vulkan;
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

    const std::string fallbackReason = makeBackendInitFallbackReason("d3d12", "mock error", "vulkan");
    if (fallbackReason.find("d3d12") == std::string::npos ||
        fallbackReason.find("mock error") == std::string::npos ||
        fallbackReason.find("vulkan") == std::string::npos) {
        outFail = "Backend init fallback reason should include active backend, error, and fallback target.";
        return false;
    }

    return true;
}

