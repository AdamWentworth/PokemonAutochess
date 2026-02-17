#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "engine/render/IRenderBackend.h"

struct SDL_Window;

#if defined(_WIN32)
#include <wrl/client.h>

struct IDXGIAdapter1;
struct IDXGIFactory4;
struct IDXGISwapChain3;
struct ID3D12CommandAllocator;
struct ID3D12CommandQueue;
struct ID3D12DescriptorHeap;
struct ID3D12Device;
struct ID3D12Fence;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
#endif

class D3D12RenderBackend final : public IRenderBackend {
public:
    D3D12RenderBackend(SDL_Window* window,
                       int width,
                       int height,
                       const std::string& preferredAdapterName = {});
    ~D3D12RenderBackend() override;

    const char* backendId() const override { return "d3d12"; }
    void beginFrame(float r, float g, float b, float a) override;
    void endFrame() override;
    void onResize(int width, int height) override;
    bool requiresOpenGLContext() const override { return false; }
    bool handlesPresentation() const override { return true; }
    std::string activeGpuName() const override { return adapterName_; }
    bool activeGpuIsDiscrete() const override { return discreteAdapter_; }
    void shutdown() override;

private:
    void initDeviceAndSwapchain(const std::string& preferredAdapterName);
    void createRenderTargets();
    void releaseRenderTargets();
    void waitForGpu();
    void ensureWindowHandle();

private:
    SDL_Window* window_ = nullptr;
    int width_ = 1;
    int height_ = 1;
    bool initialized_ = false;
    bool recording_ = false;
    bool discreteAdapter_ = false;
    std::string adapterName_;

    float clearColor_[4] = {0.1f, 0.1f, 0.1f, 1.0f};

    static constexpr std::uint32_t kFrameCount = 2;
    std::uint32_t frameIndex_ = 0;
    std::uint64_t fenceValue_ = 0;

#if defined(_WIN32)
    void* hwnd_ = nullptr;

    Microsoft::WRL::ComPtr<IDXGIFactory4> factory_;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    std::uint32_t rtvDescriptorSize_ = 0;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameCount> renderTargets_;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kFrameCount> commandAllocators_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    void* fenceEvent_ = nullptr;
#endif
};
