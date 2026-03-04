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
struct ID3D12QueryHeap;
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
    bool getLastFrameTimings(BackendFrameTimings& outTimings) const override;
    bool getLastFrameStats(BackendFrameStats& outStats) const override;
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
    void drawWorldIndexedMeshCached(const char* geometryKey,
                                    const WorldMeshVertex* vertices,
                                    std::size_t vertexCount,
                                    const std::uint32_t* indices,
                                    std::size_t indexCount,
                                    const float* viewProjectionMatrix4x4,
                                    int surfaceWidth,
                                    int surfaceHeight) override;
    void prewarmWorldIndexedMeshCached(const char* geometryKey,
                                       const WorldMeshVertex* vertices,
                                       std::size_t vertexCount,
                                       const std::uint32_t* indices,
                                       std::size_t indexCount) override;
    void drawWorldIndexedMeshTextured(const WorldMeshVertex* vertices,
                                      std::size_t vertexCount,
                                      const std::uint32_t* indices,
                                      std::size_t indexCount,
                                      const WorldTextureData* texture,
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
    void configureScreenshotCapture();

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
    SpriteTexture* ensureWorldTextureRaw(const char* key,
                                         const unsigned char* rgba,
                                         int width,
                                         int height,
                                         int wrapS,
                                         int wrapT,
                                         bool srgb);
    SpriteTexture* ensureWorldTextureRawHalfFloat(const char* key,
                                                  const std::uint16_t* rgba16f,
                                                  int width,
                                                  int height,
                                                  int wrapS,
                                                  int wrapT);
    SpriteTexture* ensureWorldTexture(const WorldTextureData* textureData);
#if defined(_WIN32)
    struct CachedWorldMesh {
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
        std::uint64_t vertexGpuAddress = 0;
        std::uint64_t indexGpuAddress = 0;
        std::size_t vertexCount = 0u;
        std::size_t indexCount = 0u;
        std::size_t vertexBytes = 0u;
        std::size_t indexBytes = 0u;
        bool valid = false;
    };
    CachedWorldMesh* ensureCachedWorldMesh(const char* geometryKey,
                                           const WorldMeshVertex* vertices,
                                           std::size_t vertexCount,
                                           const std::uint32_t* indices,
                                           std::size_t indexCount);
    void drawWorldIndexedMeshCachedInternal(const CachedWorldMesh& mesh,
                                            const float* viewProjectionMatrix4x4,
                                            int surfaceWidth,
                                            int surfaceHeight);
#endif
    void drawWorldIndexedMeshInternal(const WorldMeshVertex* vertices,
                                      std::size_t vertexCount,
                                      const std::uint32_t* indices,
                                      std::size_t indexCount,
                                      std::uint32_t baseTextureDescriptorIndex,
                                      std::uint32_t normalTextureDescriptorIndex,
                                      std::uint32_t metallicRoughnessTextureDescriptorIndex,
                                      std::uint32_t occlusionTextureDescriptorIndex,
                                      std::uint32_t emissiveTextureDescriptorIndex,
                                      std::uint32_t envTextureDescriptorIndex,
                                      const WorldTextureData* textureData,
                                      float useTexture,
                                      const float* viewProjectionMatrix4x4,
                                      int surfaceWidth,
                                      int surfaceHeight);
    void waitForGpu();
    bool waitForFenceValue(std::uint64_t fenceValue);
    void ensureWindowHandle();

private:
    SDL_Window* window_ = nullptr;
    int width_ = 1;
    int height_ = 1;
    bool initialized_ = false;
    bool recording_ = false;
    bool discreteAdapter_ = false;
    std::string adapterName_;
    float lastPresentWaitMs_ = 0.0f;
    float lastGpuFrameMs_ = 0.0f;
    bool lastGpuFrameValid_ = false;
    std::uint32_t frameDrawCalls_ = 0u;
    std::uint64_t frameTriangles_ = 0u;
    std::uint32_t lastFrameDrawCalls_ = 0u;
    std::uint64_t lastFrameTriangles_ = 0u;
    bool screenshotCaptureConfigured_ = false;
    bool screenshotCaptured_ = false;
    std::uint64_t screenshotFrameTarget_ = 0u;
    std::uint64_t frameCounter_ = 0u;
    std::string screenshotPath_;

    float clearColor_[4] = {0.1f, 0.1f, 0.1f, 1.0f};

    static constexpr std::uint32_t kFrameCount = 2;
    static constexpr std::uint32_t kTimestampQueriesPerFrame = 2;
    static constexpr std::uint32_t kTimestampQueryCount = kFrameCount * kTimestampQueriesPerFrame;
    std::uint32_t frameIndex_ = 0;
    std::uint64_t fenceValue_ = 0;
    std::uint64_t timestampFrequency_ = 0;
    std::array<std::uint64_t, kFrameCount> frameFenceValues_{};

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
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> timestampQueryHeap_;
    Microsoft::WRL::ComPtr<ID3D12Resource> timestampReadbackBuffer_;
    void* fenceEvent_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> debugRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> debugPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> debugVertexBuffer_;
    std::uint64_t debugVertexBufferGpuAddress_ = 0;
    std::uint32_t debugVertexStride_ = 0;
    std::uint32_t debugVertexBufferSize_ = 0;
    std::uint32_t debugVertexFrameOffset_ = 0;
    std::uint8_t* debugVertexMappedData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> worldRootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldBlendPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldAdditiveBlendPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> worldPremultipliedBlendPipelineState_;
    Microsoft::WRL::ComPtr<ID3D12Resource> worldVertexBuffer_;
    std::uint64_t worldVertexBufferGpuAddress_ = 0;
    std::uint32_t worldVertexStride_ = 0;
    std::uint32_t worldVertexBufferSize_ = 0;
    std::uint32_t worldVertexFrameOffset_ = 0;
    std::uint8_t* worldVertexMappedData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> worldIndexBuffer_;
    std::uint64_t worldIndexBufferGpuAddress_ = 0;
    std::uint32_t worldIndexBufferSize_ = 0;
    std::uint32_t worldIndexFrameOffset_ = 0;
    std::uint8_t* worldIndexMappedData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> worldVsConstantBuffer_;
    std::uint64_t worldVsConstantBufferGpuAddress_ = 0;
    std::uint32_t worldVsConstantBufferSize_ = 0;
    std::uint32_t worldVsConstantFrameOffset_ = 0;
    std::uint8_t* worldVsConstantMappedData_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> worldSkinMatrixBuffer_;
    std::uint64_t worldSkinMatrixBufferGpuAddress_ = 0;
    std::uint32_t worldSkinMatrixBufferSize_ = 0;
    std::uint32_t worldSkinMatrixFrameOffset_ = 0;
    std::uint8_t* worldSkinMatrixMappedData_ = nullptr;
    std::uint32_t worldFallbackTextureDescriptorIndex_ = 0;
    std::uint32_t worldFallbackNormalTextureDescriptorIndex_ = 0;
    std::uint32_t worldFallbackMetallicRoughnessTextureDescriptorIndex_ = 0;
    std::uint32_t worldFallbackOcclusionTextureDescriptorIndex_ = 0;
    std::uint32_t worldFallbackEmissiveTextureDescriptorIndex_ = 0;
    std::uint32_t worldFallbackEnvTextureDescriptorIndex_ = 0;

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
    std::uint8_t* spriteVertexMappedData_ = nullptr;
    std::unordered_map<std::string, SpriteTexture> spriteTextures_;
    std::unordered_map<std::string, SpriteTexture> worldTextures_;
    std::unordered_map<std::string, CachedWorldMesh> cachedWorldMeshes_;
#endif
};
