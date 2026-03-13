#pragma once

#include <iosfwd>
#include <string>

namespace game::runtime::renderer_activation {

struct Inputs {
    std::string requestedBackend;
    std::string preferredGpuAdapter;
    bool vsyncEnabled = false;
    bool requireDiscreteGpu = false;
    bool rendererRequiresOpenGlContext = false;
    std::string rendererBackendId;
    std::string activeGpuName;
    bool activeGpuIsDiscrete = false;
    std::string glVendor;
    std::string glRenderer;
    std::string glVersion;
    std::string glslVersion;
};

struct Outputs {
    std::string activeBackend;
    std::string gpuVendor;
    std::string gpuRenderer;
    bool gpuDiscrete = false;
    bool discreteRequirementSatisfied = true;
};

Outputs resolve(const Inputs& inputs);

void logStartupSummary(const Inputs& inputs, const Outputs& outputs, std::ostream& out);

void logPreferredAdapterMismatch(const Inputs& inputs, const Outputs& outputs, std::ostream& out);

} // namespace game::runtime::renderer_activation
