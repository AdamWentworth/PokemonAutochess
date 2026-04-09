#include <sstream>
#include <string>

#include "engine/core/EngineServices.h"
#include "game/runtime/renderer/RuntimeRendererStartupState.h"
#include "TestRenderBackendDoubles.h"

namespace {
using test::render_doubles::ConfigurableFakeRenderBackend;
using test::render_doubles::FakeRenderBackendConfig;

} // namespace

bool test_runtime_renderer_startup_state_contract(std::string& outFail) {
    {
        EngineServices services;
        services.requestedRendererBackend = "opengl";
        services.preferredGpuAdapter = "nvidia";
        services.vsyncEnabled = true;
        services.requireDiscreteGpu = true;

        ConfigurableFakeRenderBackend renderer(FakeRenderBackendConfig{
            .backendId = "opengl",
            .requiresOpenGlContext = true,
        });
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
        ConfigurableFakeRenderBackend renderer(FakeRenderBackendConfig{
            .backendId = "d3d12",
            .activeGpuName = "RTX 4080",
            .activeGpuIsDiscrete = true,
        });
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

