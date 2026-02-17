#include "game/runtime/GpuAdapters.h"

#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_6.h>
#endif

namespace game::video {
namespace {

#if defined(_WIN32)
std::string utf8FromWide(const wchar_t* wide) {
    if (wide == nullptr || *wide == L'\0') return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), needed, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}
#endif

} // namespace

std::vector<SystemGpuAdapter> enumerateSystemGpuAdapters() {
    std::vector<SystemGpuAdapter> out;

#if defined(_WIN32)
    IDXGIFactory1* factoryRaw = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factoryRaw))) || factoryRaw == nullptr) {
        return out;
    }

    IDXGIFactory1* factory = factoryRaw;
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        const HRESULT hr = factory->EnumAdapters1(i, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr) || adapter == nullptr) continue;

        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            SystemGpuAdapter info;
            info.name = utf8FromWide(desc.Description);
            const bool software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            // Simple heuristic: Intel vendor IDs are typically integrated on laptops.
            info.discrete = !software && desc.VendorId != 0x8086;
            out.push_back(std::move(info));
        }
        adapter->Release();
    }
    factory->Release();
#endif

    return out;
}

} // namespace game::video
