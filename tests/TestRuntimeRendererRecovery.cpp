#include <memory>
#include <string>

#include "game/runtime/renderer/RuntimeRendererRecovery.h"
#include "TestRenderBackendDoubles.h"

namespace {
using test::render_doubles::ConfigurableFakeRenderBackend;
using test::render_doubles::FakeRenderBackendConfig;

} // namespace

bool test_runtime_renderer_recovery_contract(std::string& outFail) {
    using game::runtime::renderer_recovery::FailureStage;
    using game::runtime::renderer_recovery::FallbackWindowResult;
    using game::runtime::renderer_recovery::Inputs;

    {
        int fallbackWindowCalls = 0;
        int openGlInitCalls = 0;
        int syncCalls = 0;
        const auto result = game::runtime::renderer_recovery::createWithFallbacks(
            Inputs{game::video::RendererBackend::D3D12, "d3d12", {game::video::RendererBackend::Vulkan}},
            [](game::video::RendererBackend backend, std::string *) -> std::unique_ptr<IRenderBackend> {
                const std::string backendName = game::video::rendererBackendName(backend);
                return std::make_unique<ConfigurableFakeRenderBackend>(FakeRenderBackendConfig{
                    .backendId = backendName,
                    .requiresOpenGlContext = backendName == "opengl",
                });
            },
            [&](game::video::RendererBackend) {
                ++fallbackWindowCalls;
                return FallbackWindowResult{};
            },
            [&](std::string *) {
                ++openGlInitCalls;
                return true;
            },
            [&]() { ++syncCalls; });
        if (!result.renderer ||
            result.activeBackend != game::video::RendererBackend::D3D12 ||
            result.rendererBackendFallback ||
            result.failureStage != FailureStage::None ||
            fallbackWindowCalls != 0 ||
            openGlInitCalls != 0 ||
            syncCalls != 0) {
            outFail = "createWithFallbacks should preserve a successful initial backend without touching fallback callbacks.";
            return false;
        }
    }

    {
        int createCalls = 0;
        int fallbackWindowCalls = 0;
        int openGlInitCalls = 0;
        int syncCalls = 0;
        const auto result = game::runtime::renderer_recovery::createWithFallbacks(
            Inputs{game::video::RendererBackend::D3D12, "d3d12", {game::video::RendererBackend::Vulkan}},
            [&](game::video::RendererBackend backend, std::string *outError) -> std::unique_ptr<IRenderBackend> {
                ++createCalls;
                if (backend == game::video::RendererBackend::D3D12) {
                    if (outError) *outError = "mock create error";
                    return std::unique_ptr<IRenderBackend>{};
                }
                const std::string backendName = game::video::rendererBackendName(backend);
                return std::make_unique<ConfigurableFakeRenderBackend>(FakeRenderBackendConfig{
                    .backendId = backendName,
                    .requiresOpenGlContext = backendName == "opengl",
                });
            },
            [&](game::video::RendererBackend) {
                ++fallbackWindowCalls;
                return FallbackWindowResult{true, {}};
            },
            [&](std::string *) {
                ++openGlInitCalls;
                return true;
            },
            [&]() { ++syncCalls; });
        if (!result.renderer ||
            result.activeBackend != game::video::RendererBackend::Vulkan ||
            !result.rendererBackendFallback ||
            result.rendererBackendFallbackReason.find("mock create error") == std::string::npos ||
            result.failureStage != FailureStage::None ||
            createCalls != 2 ||
            fallbackWindowCalls != 1 ||
            openGlInitCalls != 0 ||
            syncCalls != 1) {
            outFail = "createWithFallbacks should recreate the enrolled Vulkan path without GL initialization after a non-OpenGL backend fails to initialize.";
            return false;
        }
    }

    {
        const auto result = game::runtime::renderer_recovery::createWithFallbacks(
            Inputs{game::video::RendererBackend::D3D12, "d3d12", {game::video::RendererBackend::Vulkan}},
            [](game::video::RendererBackend, std::string *outError) -> std::unique_ptr<IRenderBackend> {
                if (outError) *outError = "mock create error";
                return std::unique_ptr<IRenderBackend>{};
            },
            [](game::video::RendererBackend) {
                return FallbackWindowResult{false, "window create failed"};
            },
            [](std::string *) { return true; },
            []() {});
        if (result.renderer ||
            result.failureStage != FailureStage::FallbackWindowOpen ||
            result.error != "window create failed") {
            outFail = "createWithFallbacks should surface fallback window creation failures.";
            return false;
        }
    }

    {
        const auto result = game::runtime::renderer_recovery::createWithFallbacks(
            Inputs{game::video::RendererBackend::OpenGL, "opengl"},
            [](game::video::RendererBackend, std::string *outError) -> std::unique_ptr<IRenderBackend> {
                if (outError) *outError = "opengl create failed";
                return std::unique_ptr<IRenderBackend>{};
            },
            [](game::video::RendererBackend) { return FallbackWindowResult{true, {}}; },
            [](std::string *) { return true; },
            []() {});
        if (result.renderer ||
            result.failureStage != FailureStage::InitialBackendCreate ||
            result.error.find("not enrolled") == std::string::npos ||
            result.rendererBackendFallback) {
            outFail = "createWithFallbacks should not attempt a second fallback for a disallowed initial renderer.";
            return false;
        }
    }

    {
        int reopened = 0;
        const auto result = game::runtime::renderer_recovery::createWithFallbacks(
            Inputs{game::video::RendererBackend::Vulkan, "vulkan", {game::video::RendererBackend::OpenGL}},
            [](game::video::RendererBackend, std::string *error) -> std::unique_ptr<IRenderBackend> {
                *error = "device unavailable";
                return {};
            },
            [&](game::video::RendererBackend) { ++reopened; return FallbackWindowResult{true, {}}; },
            [](std::string *) { return true; }, [] {});
        if (result.renderer || reopened || result.rendererBackendFallback || result.error != "device unavailable") {
            outFail = "Recovery must not try an unenrolled GL fallback";
            return false;
        }
    }
    return true;
}

