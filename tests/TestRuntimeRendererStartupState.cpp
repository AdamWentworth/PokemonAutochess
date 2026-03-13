#include <sstream>
#include <string>

#include "engine/core/EngineServices.h"
#include "engine/render/IRenderBackend.h"
#include "game/runtime/renderer/RuntimeRendererStartupState.h"

namespace {

class FakeRenderBackend final : public IRenderBackend {
public:
    FakeRenderBackend(std::string backendId, bool requiresOpenGlContext, std::string activeGpuName, bool discrete)
        : backendId_(std::move(backendId))
        , requiresOpenGlContext_(requiresOpenGlContext)
        , activeGpuName_(std::move(activeGpuName))
        , discrete_(discrete) {}

    const char* backendId() const override { return backendId_.c_str(); }
    void beginFrame(float, float, float, float) override {}
    void endFrame() override {}
    void onResize(int, int) override {}
    bool requiresOpenGLContext() const override { return requiresOpenGlContext_; }
    bool handlesPresentation() const override { return false; }
    std::string activeGpuName() const override { return activeGpuName_; }
    bool activeGpuIsDiscrete() const override { return discrete_; }
    void shutdown() override {}

private:
    std::string backendId_;
    bool requiresOpenGlContext_ = false;
    std::string activeGpuName_;
    bool discrete_ = false;
};

} // namespace

bool test_runtime_renderer_startup_state_contract(std::string& outFail) {
    {
        EngineServices services;
        services.requestedRendererBackend = "opengl";
        services.preferredGpuAdapter = "nvidia";
        services.vsyncEnabled = true;
        services.requireDiscreteGpu = true;

        FakeRenderBackend renderer("opengl", true, "", false);
        game::runtime::renderer_startup_state::OpenGlStrings glStrings;
        glStrings.vendor = "Intel";
        glStrings.renderer = "Intel Iris Xe";
        glStrings.version = "4.6";
        glStrings.glslVersion = "4.60";

        const auto inputs =
            game::runtime::renderer_startup_state::makeActivationInputs(services, renderer, glStrings);
        if (!inputs.rendererRequiresOpenGlContext ||
            inputs.rendererBackendId != "opengl" ||
            inputs.glVendor != "Intel" ||
            inputs.glRenderer != "Intel Iris Xe") {
            outFail = "makeActivationInputs should capture OpenGL renderer identity and service preferences.";
            return false;
        }

        std::ostringstream out;
        const auto outputs =
            game::runtime::renderer_startup_state::applyAndLog(services, inputs, out);
        if (services.activeRendererBackend != "opengl" ||
            services.gpuVendor != "Intel" ||
            services.gpuRenderer != "Intel Iris Xe" ||
            services.gpuDiscrete ||
            outputs.discreteRequirementSatisfied ||
            out.str().find("Preferred adapter 'nvidia' was not selected") == std::string::npos) {
            outFail = "applyAndLog should update EngineServices, preserve the resolved GPU state, and emit mismatch logging.";
            return false;
        }
    }

    {
        EngineServices services;
        services.requestedRendererBackend = "d3d12";
        FakeRenderBackend renderer("d3d12", false, "RTX 4080", true);
        const auto inputs = game::runtime::renderer_startup_state::makeActivationInputs(
            services,
            renderer,
            game::runtime::renderer_startup_state::OpenGlStrings{});
        if (inputs.rendererRequiresOpenGlContext ||
            inputs.activeGpuName != "RTX 4080") {
            outFail = "makeActivationInputs should preserve backend-reported GPU identity for native renderers.";
            return false;
        }

        std::ostringstream out;
        const auto outputs =
            game::runtime::renderer_startup_state::applyAndLog(services, inputs, out);
        if (services.activeRendererBackend != "d3d12" ||
            services.gpuVendor != "d3d12" ||
            services.gpuRenderer != "RTX 4080" ||
            !services.gpuDiscrete ||
            !outputs.discreteRequirementSatisfied ||
            out.str().find("shared gameplay render path") == std::string::npos) {
            outFail = "applyAndLog should preserve native-backend GPU identity and log the shared-path startup note.";
            return false;
        }
    }

    return true;
}

