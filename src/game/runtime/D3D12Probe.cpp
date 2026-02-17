#include "game/runtime/D3D12Probe.h"
#include "engine/render/DxgiAdapterSelection.h"

#include <string>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#endif

namespace game::video {

D3D12ProbeResult probeD3D12Adapter(const std::string& preferredAdapterName) {
    D3D12ProbeResult out;
    out.preferredAdapterRequested = !preferredAdapterName.empty();

#if !defined(_WIN32)
    out.message = "D3D12 probe is only available on Windows.";
    return out;
#else
    out.supported = true;

    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory) {
        out.message = "CreateDXGIFactory1 failed.";
        return out;
    }

    const auto selection = engine::render::dxgi::selectHardwareAdapter(factory.Get(), preferredAdapterName);
    out.preferredAdapterMatched = selection.preferredMatched;
    if (!selection.adapter) {
        out.message = "No hardware DXGI adapter available for D3D12.";
        return out;
    }
    out.selectedAdapter = selection.name.empty() ? "<unnamed dxgi adapter>" : selection.name;

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    const HRESULT deviceHr = D3D12CreateDevice(
        selection.adapter.Get(),
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(device.ReleaseAndGetAddressOf()));

    if (FAILED(deviceHr) || !device) {
        out.message = "D3D12CreateDevice failed for adapter '" + out.selectedAdapter + "'.";
        return out;
    }

    out.initialized = true;
    out.message = "D3D12 device creation succeeded.";
    if (out.preferredAdapterRequested && !out.preferredAdapterMatched) {
        out.message += " Preferred adapter not found; selected best available adapter.";
    }
    return out;
#endif
}

} // namespace game::video
