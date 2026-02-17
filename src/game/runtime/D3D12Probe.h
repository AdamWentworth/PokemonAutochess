#pragma once

#include <string>

namespace game::video {

struct D3D12ProbeResult {
    bool supported = false;
    bool initialized = false;
    bool preferredAdapterRequested = false;
    bool preferredAdapterMatched = false;
    std::string selectedAdapter;
    std::string message;
};

D3D12ProbeResult probeD3D12Adapter(const std::string& preferredAdapterName);

} // namespace game::video
