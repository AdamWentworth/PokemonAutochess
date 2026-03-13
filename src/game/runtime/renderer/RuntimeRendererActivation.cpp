#include "game/runtime/renderer/RuntimeRendererActivation.h"

#include "game/runtime/renderer/RendererStartupDiagnostics.h"

#include <ostream>

namespace {

bool looksIntegratedGpu(const std::string& vendor, const std::string& renderer) {
    return game::runtime::startup_diag::activeRendererMatchesPreferredAdapter(vendor, "intel") ||
        game::runtime::startup_diag::activeRendererMatchesPreferredAdapter(renderer, "intel");
}

} // namespace

namespace game::runtime::renderer_activation {

Outputs resolve(const Inputs& inputs) {
    Outputs out;
    out.activeBackend = inputs.rendererBackendId;
    out.gpuRenderer = inputs.activeGpuName;
    out.gpuDiscrete = inputs.activeGpuIsDiscrete;

    if (inputs.rendererRequiresOpenGlContext) {
        out.gpuVendor = inputs.glVendor;
        if (out.gpuRenderer.empty()) {
            out.gpuRenderer = inputs.glRenderer;
        }
        out.gpuDiscrete = !looksIntegratedGpu(out.gpuVendor, out.gpuRenderer);
    } else {
        out.gpuVendor = "d3d12";
        if (out.gpuRenderer.empty()) {
            out.gpuRenderer = "<unknown d3d12 adapter>";
        }
    }

    out.discreteRequirementSatisfied = !inputs.requireDiscreteGpu || out.gpuDiscrete;
    return out;
}

void logStartupSummary(const Inputs& inputs, const Outputs& outputs, std::ostream& out) {
    if (!inputs.rendererRequiresOpenGlContext) {
        out << "[Renderer] D3D12 backend initialized with shared gameplay render path.\n";
    }

    game::runtime::startup_diag::ActiveRendererSummary summary;
    summary.requestedBackend = inputs.requestedBackend;
    summary.activeBackend = outputs.activeBackend;
    summary.gpuVendor = outputs.gpuVendor;
    summary.gpuRenderer = outputs.gpuRenderer;
    summary.gpuDiscrete = outputs.gpuDiscrete;
    summary.vsyncEnabled = inputs.vsyncEnabled;
    summary.hasOpenGlStrings = inputs.rendererRequiresOpenGlContext;
    summary.glVersion = inputs.glVersion;
    summary.glslVersion = inputs.glslVersion;
    game::runtime::startup_diag::logActiveRendererSummary(summary, out);
}

void logPreferredAdapterMismatch(const Inputs& inputs, const Outputs& outputs, std::ostream& out) {
    game::runtime::startup_diag::logPreferredActiveAdapterMismatch(
        inputs.preferredGpuAdapter,
        outputs.gpuRenderer,
        out);
}

} // namespace game::runtime::renderer_activation

