#include "game/runtime/D3D12Probe.h"

#include <algorithm>
#include <cctype>
#include <string>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif

namespace game::video {
namespace {

std::string toLowerCopy(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool containsCi(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    return toLowerCopy(haystack).find(toLowerCopy(needle)) != std::string::npos;
}

#if defined(_WIN32)
std::string utf8FromWide(const wchar_t* wide) {
    if (!wide || *wide == L'\0') return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), needed, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

bool isSoftwareAdapter(const DXGI_ADAPTER_DESC1& desc) {
    return (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
}

bool isLikelyDiscreteAdapter(const DXGI_ADAPTER_DESC1& desc) {
    if (isSoftwareAdapter(desc)) return false;
    // Heuristic: Intel vendor IDs are typically integrated on hybrid laptops.
    return desc.VendorId != 0x8086;
}
#endif

} // namespace

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

    Microsoft::WRL::ComPtr<IDXGIAdapter1> selectedAdapter;
    DXGI_ADAPTER_DESC1 selectedDesc{};
    bool hasSelection = false;

    for (UINT index = 0;; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        const HRESULT hr = factory->EnumAdapters1(index, adapter.ReleaseAndGetAddressOf());
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr) || !adapter) continue;

        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc))) continue;
        if (isSoftwareAdapter(desc)) continue;

        const std::string adapterName = utf8FromWide(desc.Description);
        const bool preferredMatch = out.preferredAdapterRequested &&
                                    containsCi(adapterName, preferredAdapterName);
        const bool candidateDiscrete = isLikelyDiscreteAdapter(desc);

        if (preferredMatch) {
            selectedAdapter = adapter;
            selectedDesc = desc;
            hasSelection = true;
            out.preferredAdapterMatched = true;
            break;
        }

        if (!hasSelection) {
            selectedAdapter = adapter;
            selectedDesc = desc;
            hasSelection = true;
            continue;
        }

        const bool selectedDiscrete = isLikelyDiscreteAdapter(selectedDesc);
        if (!selectedDiscrete && candidateDiscrete) {
            selectedAdapter = adapter;
            selectedDesc = desc;
        }
    }

    if (!hasSelection || !selectedAdapter) {
        out.message = "No hardware DXGI adapter available for D3D12.";
        return out;
    }

    out.selectedAdapter = utf8FromWide(selectedDesc.Description);

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    const HRESULT deviceHr = D3D12CreateDevice(
        selectedAdapter.Get(),
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
