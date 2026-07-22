#include <sstream>
#include <string>

#include "game/runtime/renderer/RuntimeRendererActivation.h"

bool test_runtime_renderer_activation_contract(std::string& outFail) {
    {
        game::runtime::renderer_activation::Inputs inputs;
        inputs.requestedBackend = "auto";
        inputs.preferredGpuAdapter = "nvidia";
        inputs.vsyncEnabled = true;
        inputs.requireDiscreteGpu = true;
        inputs.rendererRequiresOpenGlContext = true;
        inputs.rendererBackendId = "opengl";
        inputs.glVendor = "Intel";
        inputs.glRenderer = "Intel Iris Xe";
        inputs.glVersion = "4.6";
        inputs.glslVersion = "4.60";

        const auto outputs = game::runtime::renderer_activation::resolve(inputs);
        if (outputs.activeBackend != "opengl" ||
            outputs.gpuVendor != "Intel" ||
            outputs.gpuRenderer != "Intel Iris Xe" ||
            outputs.gpuDiscrete ||
            outputs.discreteRequirementSatisfied) {
            outFail = "resolve should derive OpenGL GPU identity from GL strings and flag failed discrete-GPU requirements.";
            return false;
        }

        std::ostringstream out;
        game::runtime::renderer_activation::logStartupSummary(inputs, outputs, out);
        game::runtime::renderer_activation::logPreferredAdapterMismatch(inputs, outputs, out);
        const std::string log = out.str();
        if (log.find("[Renderer] Active:    opengl") == std::string::npos ||
            log.find("[GPU] Preferred adapter 'nvidia' was not selected") == std::string::npos) {
            outFail = "renderer activation logging should include the startup summary and preferred-adapter mismatch warning.";
            return false;
        }
    }

    {
        game::runtime::renderer_activation::Inputs inputs;
        inputs.requestedBackend = "d3d12";
        inputs.rendererRequiresOpenGlContext = false;
        inputs.rendererBackendId = "d3d12";
        inputs.activeGpuIsDiscrete = true;

        const auto outputs = game::runtime::renderer_activation::resolve(inputs);
        if (outputs.gpuVendor != "d3d12" ||
            outputs.gpuRenderer != "<unknown d3d12 adapter>" ||
            !outputs.gpuDiscrete ||
            !outputs.discreteRequirementSatisfied) {
            outFail = "resolve should synthesize the D3D12 GPU identity when the backend does not expose an adapter name.";
            return false;
        }

        std::ostringstream out;
        game::runtime::renderer_activation::logStartupSummary(inputs, outputs, out);
        if (out.str().find("shared gameplay render path") == std::string::npos) {
            outFail = "logStartupSummary should preserve the D3D12 shared-path startup note.";
            return false;
        }
    }

    {
        game::runtime::renderer_activation::Inputs inputs;
        inputs.requestedBackend = "vulkan";
        inputs.rendererRequiresOpenGlContext = false;
        inputs.rendererBackendId = "vulkan";
        inputs.activeGpuName = "Mock Vulkan GPU";
        inputs.activeGpuIsDiscrete = true;

        const auto outputs = game::runtime::renderer_activation::resolve(inputs);
        if (outputs.gpuVendor != "vulkan" ||
            outputs.gpuRenderer != "Mock Vulkan GPU" ||
            !outputs.gpuDiscrete) {
            outFail = "resolve should preserve Vulkan backend GPU identity.";
            return false;
        }
    }

    return true;
}

