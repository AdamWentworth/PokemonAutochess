#include "engine/render/D3D12RenderBackend.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

#include <SDL2/SDL.h>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <SDL2/SDL_syswm.h>
#endif

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

bool isLikelyDiscrete(const DXGI_ADAPTER_DESC1& desc) {
    if (isSoftwareAdapter(desc)) return false;
    return desc.VendorId != 0x8086;
}
#endif

} // namespace

D3D12RenderBackend::D3D12RenderBackend(SDL_Window* window,
                                       int width,
                                       int height,
                                       const std::string& preferredAdapterName)
    : window_(window)
    , width_((width > 1) ? width : 1)
    , height_((height > 1) ? height : 1) {
    if (!window_) {
        throw std::runtime_error("D3D12RenderBackend requires a valid SDL_Window.");
    }
    initDeviceAndSwapchain(preferredAdapterName);
}

D3D12RenderBackend::~D3D12RenderBackend() {
    shutdown();
}

void D3D12RenderBackend::beginFrame(float r, float g, float b, float a) {
    clearColor_[0] = r;
    clearColor_[1] = g;
    clearColor_[2] = b;
    clearColor_[3] = a;

#if defined(_WIN32)
    if (!initialized_ || !device_ || !swapChain_ || !commandList_) return;

    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
    auto& allocator = commandAllocators_[frameIndex_];
    if (!allocator) return;

    if (FAILED(allocator->Reset())) return;
    if (FAILED(commandList_->Reset(allocator.Get(), nullptr))) return;

    D3D12_RESOURCE_BARRIER toRtv{};
    toRtv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRtv.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    toRtv.Transition.pResource = renderTargets_[frameIndex_].Get();
    toRtv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toRtv.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toRtv.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList_->ResourceBarrier(1, &toRtv);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += static_cast<SIZE_T>(frameIndex_) * static_cast<SIZE_T>(rtvDescriptorSize_);

    commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    commandList_->ClearRenderTargetView(rtvHandle, clearColor_, 0, nullptr);

    recording_ = true;
#else
    (void)r;
    (void)g;
    (void)b;
    (void)a;
#endif
}

void D3D12RenderBackend::endFrame() {
#if defined(_WIN32)
    if (!initialized_ || !recording_ || !commandList_ || !swapChain_ || !commandQueue_) return;

    D3D12_RESOURCE_BARRIER toPresent{};
    toPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toPresent.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    toPresent.Transition.pResource = renderTargets_[frameIndex_].Get();
    toPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList_->ResourceBarrier(1, &toPresent);

    if (FAILED(commandList_->Close())) {
        recording_ = false;
        return;
    }

    ID3D12CommandList* commandLists[] = {commandList_.Get()};
    commandQueue_->ExecuteCommandLists(1, commandLists);
    swapChain_->Present(1, 0);
    waitForGpu();

    recording_ = false;
#endif
}

void D3D12RenderBackend::onResize(int width, int height) {
#if defined(_WIN32)
    const int newW = (width > 1) ? width : 1;
    const int newH = (height > 1) ? height : 1;
    if (!initialized_ || ((newW == width_) && (newH == height_))) return;

    width_ = newW;
    height_ = newH;
    if (!swapChain_ || !device_) return;

    waitForGpu();
    recording_ = false;
    releaseRenderTargets();

    if (SUCCEEDED(swapChain_->ResizeBuffers(
            kFrameCount,
            static_cast<UINT>(width_),
            static_cast<UINT>(height_),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            0))) {
        frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
        createRenderTargets();
    }
#else
    (void)width;
    (void)height;
#endif
}

void D3D12RenderBackend::shutdown() {
#if defined(_WIN32)
    if (!initialized_) return;

    waitForGpu();
    releaseRenderTargets();

    commandList_.Reset();
    for (auto& allocator : commandAllocators_) allocator.Reset();
    rtvHeap_.Reset();
    swapChain_.Reset();
    commandQueue_.Reset();
    fence_.Reset();
    device_.Reset();
    adapter_.Reset();
    factory_.Reset();

    if (fenceEvent_) {
        CloseHandle(static_cast<HANDLE>(fenceEvent_));
        fenceEvent_ = nullptr;
    }
#endif
    initialized_ = false;
}

void D3D12RenderBackend::initDeviceAndSwapchain(const std::string& preferredAdapterName) {
#if !defined(_WIN32)
    (void)preferredAdapterName;
    throw std::runtime_error("D3D12RenderBackend is only available on Windows.");
#else
    ensureWindowHandle();

    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory_.ReleaseAndGetAddressOf()))) || !factory_) {
        throw std::runtime_error("CreateDXGIFactory1 failed for D3D12 backend.");
    }

    bool selected = false;
    DXGI_ADAPTER_DESC1 selectedDesc{};

    for (UINT i = 0;; ++i) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
        const HRESULT hr = factory_->EnumAdapters1(i, candidate.ReleaseAndGetAddressOf());
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr) || !candidate) continue;

        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(candidate->GetDesc1(&desc))) continue;
        if (isSoftwareAdapter(desc)) continue;

        const std::string name = utf8FromWide(desc.Description);
        const bool preferred = !preferredAdapterName.empty() && containsCi(name, preferredAdapterName);
        const bool discrete = isLikelyDiscrete(desc);

        if (!selected) {
            adapter_ = candidate;
            selectedDesc = desc;
            selected = true;
            continue;
        }

        if (preferred) {
            adapter_ = candidate;
            selectedDesc = desc;
            selected = true;
            break;
        }

        const bool selectedDiscrete = isLikelyDiscrete(selectedDesc);
        if (!selectedDiscrete && discrete) {
            adapter_ = candidate;
            selectedDesc = desc;
            selected = true;
        }
    }

    if (!selected || !adapter_) {
        throw std::runtime_error("No suitable DXGI adapter found for D3D12 backend.");
    }

    adapterName_ = utf8FromWide(selectedDesc.Description);
    discreteAdapter_ = isLikelyDiscrete(selectedDesc);

    if (FAILED(D3D12CreateDevice(adapter_.Get(),
                                 D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(device_.ReleaseAndGetAddressOf()))) ||
        !device_) {
        throw std::runtime_error("D3D12CreateDevice failed.");
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    if (FAILED(device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(commandQueue_.ReleaseAndGetAddressOf()))) ||
        !commandQueue_) {
        throw std::runtime_error("CreateCommandQueue failed.");
    }

    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.BufferCount = kFrameCount;
    swapDesc.Width = static_cast<UINT>(width_);
    swapDesc.Height = static_cast<UINT>(height_);
    swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapDesc.Scaling = DXGI_SCALING_STRETCH;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
    if (FAILED(factory_->CreateSwapChainForHwnd(commandQueue_.Get(),
                                                static_cast<HWND>(hwnd_),
                                                &swapDesc,
                                                nullptr,
                                                nullptr,
                                                swapChain1.ReleaseAndGetAddressOf())) ||
        !swapChain1) {
        throw std::runtime_error("CreateSwapChainForHwnd failed.");
    }
    if (FAILED(factory_->MakeWindowAssociation(static_cast<HWND>(hwnd_), DXGI_MWA_NO_ALT_ENTER))) {
        // Non-fatal.
    }
    if (FAILED(swapChain1.As(&swapChain_)) || !swapChain_) {
        throw std::runtime_error("Query IDXGISwapChain3 failed.");
    }
    frameIndex_ = swapChain_->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.NumDescriptors = kFrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device_->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap_.ReleaseAndGetAddressOf()))) ||
        !rtvHeap_) {
        throw std::runtime_error("CreateDescriptorHeap (RTV) failed.");
    }
    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (std::uint32_t i = 0; i < kFrameCount; ++i) {
        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(commandAllocators_[i].ReleaseAndGetAddressOf()))) ||
            !commandAllocators_[i]) {
            throw std::runtime_error("CreateCommandAllocator failed.");
        }
    }

    if (FAILED(device_->CreateCommandList(0,
                                          D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          commandAllocators_[0].Get(),
                                          nullptr,
                                          IID_PPV_ARGS(commandList_.ReleaseAndGetAddressOf()))) ||
        !commandList_) {
        throw std::runtime_error("CreateCommandList failed.");
    }
    commandList_->Close();

    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.ReleaseAndGetAddressOf()))) ||
        !fence_) {
        throw std::runtime_error("CreateFence failed.");
    }
    fenceValue_ = 0;

    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) {
        throw std::runtime_error("CreateEvent failed for D3D12 fence.");
    }

    createRenderTargets();
    initialized_ = true;
#endif
}

void D3D12RenderBackend::createRenderTargets() {
#if defined(_WIN32)
    D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t i = 0; i < kFrameCount; ++i) {
        renderTargets_[i].Reset();
        if (SUCCEEDED(swapChain_->GetBuffer(i, IID_PPV_ARGS(renderTargets_[i].ReleaseAndGetAddressOf()))) &&
            renderTargets_[i]) {
            device_->CreateRenderTargetView(renderTargets_[i].Get(), nullptr, handle);
        }
        handle.ptr += static_cast<SIZE_T>(rtvDescriptorSize_);
    }
#endif
}

void D3D12RenderBackend::releaseRenderTargets() {
#if defined(_WIN32)
    for (auto& target : renderTargets_) {
        target.Reset();
    }
#endif
}

void D3D12RenderBackend::waitForGpu() {
#if defined(_WIN32)
    if (!initialized_ || !commandQueue_ || !fence_ || !fenceEvent_) return;

    const std::uint64_t signal = ++fenceValue_;
    if (FAILED(commandQueue_->Signal(fence_.Get(), signal))) return;

    if (fence_->GetCompletedValue() < signal) {
        if (SUCCEEDED(fence_->SetEventOnCompletion(signal, static_cast<HANDLE>(fenceEvent_)))) {
            WaitForSingleObject(static_cast<HANDLE>(fenceEvent_), INFINITE);
        }
    }
#endif
}

void D3D12RenderBackend::ensureWindowHandle() {
#if defined(_WIN32)
    if (hwnd_) return;

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (!SDL_GetWindowWMInfo(window_, &wmInfo)) {
        throw std::runtime_error(std::string("SDL_GetWindowWMInfo failed: ") + SDL_GetError());
    }
    hwnd_ = wmInfo.info.win.window;
    if (!hwnd_) {
        throw std::runtime_error("Failed to resolve HWND from SDL window.");
    }
#endif
}
