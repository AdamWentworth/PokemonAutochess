#include <memory>
#include <string>

#include "engine/render/IRenderBackend.h"
#include "game/runtime/RuntimeRendererRecovery.h"

namespace {

class FakeRenderBackend final : public IRenderBackend {
public:
    explicit FakeRenderBackend(std::string backendId)
        : backendId_(std::move(backendId)) {}

    const char* backendId() const override { return backendId_.c_str(); }
    void beginFrame(float, float, float, float) override {}
    void endFrame() override {}
    void onResize(int, int) override {}
    bool requiresOpenGLContext() const override { return backendId_ == "opengl"; }
    bool handlesPresentation() const override { return false; }
    void shutdown() override {}

private:
    std::string backendId_;
};

} // namespace

bool test_runtime_renderer_recovery_contract(std::string& outFail) {
    using game::runtime::renderer_recovery::FailureStage;
    using game::runtime::renderer_recovery::Inputs;
    using game::runtime::renderer_recovery::OpenGlWindowResult;

    {
        int fallbackWindowCalls = 0;
        int openGlInitCalls = 0;
        int syncCalls = 0;
        const auto result = game::runtime::renderer_recovery::createWithOpenGlFallback(
            Inputs{game::video::RendererBackend::D3D12, "d3d12"},
            [](game::video::RendererBackend backend, std::string*) -> std::unique_ptr<IRenderBackend> {
                return std::make_unique<FakeRenderBackend>(game::video::rendererBackendName(backend));
            },
            [&]() {
                ++fallbackWindowCalls;
                return OpenGlWindowResult{};
            },
            [&](std::string*) {
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
            outFail = "createWithOpenGlFallback should preserve a successful initial backend without touching fallback callbacks.";
            return false;
        }
    }

    {
        int createCalls = 0;
        int fallbackWindowCalls = 0;
        int openGlInitCalls = 0;
        int syncCalls = 0;
        const auto result = game::runtime::renderer_recovery::createWithOpenGlFallback(
            Inputs{game::video::RendererBackend::D3D12, "d3d12"},
            [&](game::video::RendererBackend backend, std::string* outError) -> std::unique_ptr<IRenderBackend> {
                ++createCalls;
                if (backend == game::video::RendererBackend::D3D12) {
                    if (outError) *outError = "mock create error";
                    return std::unique_ptr<IRenderBackend>{};
                }
                return std::make_unique<FakeRenderBackend>(game::video::rendererBackendName(backend));
            },
            [&]() {
                ++fallbackWindowCalls;
                return OpenGlWindowResult{true, {}};
            },
            [&](std::string*) {
                ++openGlInitCalls;
                return true;
            },
            [&]() { ++syncCalls; });
        if (!result.renderer ||
            result.activeBackend != game::video::RendererBackend::OpenGL ||
            !result.rendererBackendFallback ||
            result.rendererBackendFallbackReason.find("mock create error") == std::string::npos ||
            result.failureStage != FailureStage::None ||
            createCalls != 2 ||
            fallbackWindowCalls != 1 ||
            openGlInitCalls != 1 ||
            syncCalls != 1) {
            outFail = "createWithOpenGlFallback should recreate the OpenGL path after a non-OpenGL backend fails to initialize.";
            return false;
        }
    }

    {
        const auto result = game::runtime::renderer_recovery::createWithOpenGlFallback(
            Inputs{game::video::RendererBackend::D3D12, "d3d12"},
            [](game::video::RendererBackend, std::string* outError) -> std::unique_ptr<IRenderBackend> {
                if (outError) *outError = "mock create error";
                return std::unique_ptr<IRenderBackend>{};
            },
            []() {
                return OpenGlWindowResult{false, "window create failed"};
            },
            [](std::string*) { return true; },
            []() {});
        if (result.renderer ||
            result.failureStage != FailureStage::FallbackWindowOpen ||
            result.error != "window create failed") {
            outFail = "createWithOpenGlFallback should surface fallback window creation failures.";
            return false;
        }
    }

    {
        const auto result = game::runtime::renderer_recovery::createWithOpenGlFallback(
            Inputs{game::video::RendererBackend::OpenGL, "opengl"},
            [](game::video::RendererBackend, std::string* outError) -> std::unique_ptr<IRenderBackend> {
                if (outError) *outError = "opengl create failed";
                return std::unique_ptr<IRenderBackend>{};
            },
            []() { return OpenGlWindowResult{true, {}}; },
            [](std::string*) { return true; },
            []() {});
        if (result.renderer ||
            result.failureStage != FailureStage::InitialBackendCreate ||
            result.error != "opengl create failed" ||
            result.rendererBackendFallback) {
            outFail = "createWithOpenGlFallback should not attempt a second fallback when OpenGL itself fails to initialize.";
            return false;
        }
    }

    return true;
}
