#include "game/runtime/GpuAdapters.h"
#include "engine/render/DxgiAdapterSelection.h"

#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl/client.h>
#endif

namespace game::video {

std::vector<SystemGpuAdapter> enumerateSystemGpuAdapters() {
    std::vector<SystemGpuAdapter> out;

#if defined(_WIN32)
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()))) || !factory) {
        return out;
    }

    const auto adapters = engine::render::dxgi::enumerateHardwareAdapters(factory.Get());
    out.reserve(adapters.size());
    for (const auto& adapter : adapters) {
        SystemGpuAdapter info;
        info.name = adapter.name.empty() ? "<unnamed dxgi adapter>" : adapter.name;
        info.discrete = adapter.discrete;
        out.push_back(std::move(info));
    }
#endif

    return out;
}

} // namespace game::video
