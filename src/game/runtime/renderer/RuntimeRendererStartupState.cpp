#include "game/runtime/renderer/RuntimeRendererStartupState.h"

#include "engine/render/IRenderBackend.h"

namespace game::runtime::renderer_startup_state {

game::runtime::renderer_activation::Inputs makeActivationInputs(const EngineServices& services,
                                                                const IRenderBackend& renderer,
                                                                const OpenGlStrings& openGlStrings) {
    game::runtime::renderer_activation::Inputs inputs;
    inputs.requestedBackend = services.requestedRendererBackend;
    inputs.preferredGpuAdapter = services.preferredGpuAdapter;
    inputs.vsyncEnabled = services.vsyncEnabled;
    inputs.requireDiscreteGpu = services.requireDiscreteGpu;
    inputs.rendererRequiresOpenGlContext = renderer.requiresOpenGLContext();
    inputs.rendererBackendId = renderer.backendId();
    inputs.activeGpuName = renderer.activeGpuName();
    inputs.activeGpuIsDiscrete = renderer.activeGpuIsDiscrete();
    if (inputs.rendererRequiresOpenGlContext) {
        inputs.glVendor = openGlStrings.vendor;
        inputs.glRenderer = openGlStrings.renderer;
        inputs.glVersion = openGlStrings.version;
        inputs.glslVersion = openGlStrings.glslVersion;
    }
    return inputs;
}

game::runtime::renderer_activation::Outputs applyAndLog(EngineServices& services,
                                                        const game::runtime::renderer_activation::Inputs& inputs,
                                                        std::ostream& logOut) {
    const auto outputs = game::runtime::renderer_activation::resolve(inputs);
    services.activeRendererBackend = outputs.activeBackend;
    services.gpuVendor = outputs.gpuVendor;
    services.gpuRenderer = outputs.gpuRenderer;
    services.gpuDiscrete = outputs.gpuDiscrete;
    game::runtime::renderer_activation::logStartupSummary(inputs, outputs, logOut);
    game::runtime::renderer_activation::logPreferredAdapterMismatch(inputs, outputs, logOut);
    return outputs;
}

} // namespace game::runtime::renderer_startup_state

