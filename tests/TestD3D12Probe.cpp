#include <string>

#include "game/runtime/video/D3D12Probe.h"

bool test_d3d12_probe_contract(std::string& outFail) {
    const game::video::D3D12ProbeResult result = game::video::probeD3D12Adapter("");

#if defined(_WIN32)
    if (!result.supported) {
        outFail = "D3D12 probe should report supported=true on Windows.";
        return false;
    }
    if (result.initialized && result.selectedAdapter.empty()) {
        outFail = "Initialized D3D12 probe must include selected adapter name.";
        return false;
    }
#else
    if (result.supported) {
        outFail = "D3D12 probe should report unsupported on non-Windows.";
        return false;
    }
#endif

    return true;
}
