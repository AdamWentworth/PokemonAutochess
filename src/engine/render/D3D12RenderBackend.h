#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

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
struct ID3D12PipelineState;
struct ID3D12Resource;
struct ID3D12RootSignature;
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
    bool supportsWorldTriangles3D() const override { return true; }
    bool supportsWorldIndexedMeshes() const override { return true; }
    void drawWorldTriangles(const WorldTriangle* triangles,
                            std::size_t triangleCount,
                            const float* viewProjectionMatrix4x4,
                            int surfaceWidth,
                            int surfaceHeight) override;
    void drawWorldIndexedMesh(const WorldMeshVertex* vertices,
                              std::size_t vertexCount,
                              const std::uint32_t* indices,
                              std::size_t indexCount,
                              const float* viewProjectionMatrix4x4,
                              int surfaceWidth,
                              int surfaceHeight) override;
    void drawDebugQuads(const DebugQuad* quads,
                        std::size_t quadCount,
                        int surfaceWidth,
                        int surfaceHeight) override;
    void drawDebugLines(const DebugLine* lines,
                        std::size_t lineCount,
                        int surfaceWidth,
                        int surfaceHeight) override;
    void drawDebugTriangles(const DebugTriangle* triangles,
                            std::size_t triangleCount,
                            int surfaceWidth,
                            int surfaceHeight) override;
    void drawDebugSprites(const DebugSprite* sprites,
                          std::size_t spriteCount,
                          int surfaceWidth,
                          int surfaceHeight) override;
    void shutdown() override;

private:
    void initDeviceAndSwapchain(const std::string& preferredAdapterName);
    void createRenderTargets();
    void releaseRenderTargets();
    void createDepthResources();
    void releaseDepthResources();
    void createDebugPipeline();
    void createWorldPipeline();
    void createSpritePipeline();
    struct SpriteTexture;
    SpriteTexture* ensureSpriteTexture(const std::string& texturePath);
    SpriteTexture* ensureFallbackSpriteTexture();
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
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    std::uint32_t rtvDescriptorSize_ = 0;
    std::uint32_t dsvDescriptorSize_ = 0;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameCount> renderTargets_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameCount> depthBuffers_;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, kFrameCount> commandAllocators_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    void* fenceEvent_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> debugRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> debugPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> debugVertexBuffer_;
    std::uint64_t debugVertexBufferGpuAddress_ = 0;
    std::uint32_t debugVertexStride_ = 0;
    std::uint32_t debugVertexBufferSize_ = 0;
    std::uint32_t debugVertexFrameOffset_ = 0;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> worldRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> worldVertexBuffer_;
    std::uint64_t worldVertexBufferGpuAddress_ = 0;
    std::uint32_t worldVertexStride_ = 0;
    std::uint32_t worldVertexBufferSize_ = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> worldIndexBuffer_;
    std::uint64_t worldIndexBufferGpuAddress_ = 0;
    std::uint32_t worldIndexBufferSize_ = 0;

    struct SpriteTexture {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        std::uint32_t descriptorIndex = 0;
        bool valid = false;
    };
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
    std::uint32_t srvDescriptorSize_ = 0;
    std::uint32_t nextSrvDescriptorIndex_ = 0;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> spriteRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> spritePipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> spriteVertexBuffer_;
    std::uint64_t spriteVertexBufferGpuAddress_ = 0;
    std::uint32_t spriteVertexStride_ = 0;
    std::uint32_t spriteVertexBufferSize_ = 0;
    std::uint32_t spriteVertexFrameOffset_ = 0;
    std::unordered_map<std::string, SpriteTexture> spriteTextures_;
#endif
};
