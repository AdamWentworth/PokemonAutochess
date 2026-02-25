#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/DxgiAdapterSelection.h"
#include "engine/render/DebugGeometry.h"
#include "engine/render/d3d12/D3D12TextureUpload.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <SDL2/SDL.h>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <SDL2/SDL_syswm.h>
#include <stb_image.h>
#endif

namespace {
#if defined(_WIN32)
using DebugVertex = engine::render::debug::Vertex2D;

struct SpriteVertex {
    float x;
    float y;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
};

struct WorldVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
};

static_assert(
    sizeof(WorldVertex) == sizeof(IRenderBackend::WorldMeshVertex),
    "WorldVertex and WorldMeshVertex layout must match for fast memcpy upload path.");
static_assert(std::is_trivially_copyable_v<WorldVertex>, "WorldVertex must be trivially copyable.");
static_assert(
    std::is_trivially_copyable_v<IRenderBackend::WorldMeshVertex>,
    "WorldMeshVertex must be trivially copyable.");

constexpr std::size_t kMaxSpriteQuads = 2048;
constexpr std::size_t kMaxSpriteVertices = kMaxSpriteQuads * 6;
constexpr std::size_t kMaxDebugQuads = 4096;
constexpr std::size_t kMaxDebugLines = 8192;
constexpr std::size_t kMaxDebugTriangles = 65536;
constexpr std::size_t kMaxDebugVertices = kMaxDebugTriangles * 3;
constexpr std::size_t kMaxWorldTriangles = 180000;
constexpr std::size_t kMaxWorldVertices = kMaxWorldTriangles * 3;
constexpr std::size_t kMaxWorldIndices = kMaxWorldTriangles * 3;
constexpr std::size_t kMaxSrvDescriptors = 2048;
constexpr const char* kFallbackSpriteTextureKey = "__fallback_sprite_texture__";
constexpr int kGlRepeat = 10497;
constexpr int kGlMirroredRepeat = 33648;
constexpr int kGlClampToEdge = 33071;

std::size_t alignUp(std::size_t value, std::size_t alignment) {
    if (alignment == 0u) return value;
    const std::size_t mask = alignment - 1u;
    return (value + mask) & ~mask;
}

float sanitizeWrapMode(int wrapMode) {
    if (wrapMode == kGlClampToEdge || wrapMode == kGlMirroredRepeat || wrapMode == kGlRepeat) {
        return static_cast<float>(wrapMode);
    }
    return static_cast<float>(kGlRepeat);
}

struct WorldPsConstants {
    float useTexture = 0.0f;
    float wrapS = static_cast<float>(kGlRepeat);
    float wrapT = static_cast<float>(kGlRepeat);
    float alphaMode = 0.0f;
    float alphaCutoff = 0.5f;
    float materialMode = 0.0f;
    float materialTimeSec = 0.0f;
    float materialFlags = 0.0f;
    float materialAtlasWidth = 0.0f;
    float materialAtlasHeight = 0.0f;
    float materialRect0U = 0.0f;
    float materialRect0V = 0.0f;
    float materialRect0W = 1.0f;
    float materialRect0H = 1.0f;
    float materialRect1U = 0.0f;
    float materialRect1V = 0.0f;
    float materialRect1W = 1.0f;
    float materialRect1H = 1.0f;
    float materialFlipbook0Cols = 1.0f;
    float materialFlipbook0Rows = 1.0f;
    float materialFlipbook0Frames = 1.0f;
    float materialFlipbook0Fps = 0.0f;
    float materialFlipbook1Cols = 1.0f;
    float materialFlipbook1Rows = 1.0f;
    float materialFlipbook1Frames = 1.0f;
    float materialFlipbook1Fps = 0.0f;
};

WorldPsConstants makeWorldPsConstants(const IRenderBackend::WorldTextureData* textureData, float useTexture) {
    WorldPsConstants constants;
    constants.useTexture = useTexture;
    if (!textureData) return constants;
    constants.wrapS = sanitizeWrapMode(textureData->wrapS);
    constants.wrapT = sanitizeWrapMode(textureData->wrapT);
    constants.alphaMode = static_cast<float>(std::min<std::uint8_t>(2u, textureData->alphaMode));
    constants.alphaCutoff = std::clamp(textureData->alphaCutoff, 0.0f, 1.0f);
    constants.materialMode = static_cast<float>(textureData->materialMode);
    constants.materialTimeSec = textureData->materialTimeSec;
    constants.materialFlags = textureData->materialFlags;
    constants.materialAtlasWidth = (std::max)(0.0f, textureData->materialAtlasWidth);
    constants.materialAtlasHeight = (std::max)(0.0f, textureData->materialAtlasHeight);
    constants.materialRect0U = textureData->materialRect0U;
    constants.materialRect0V = textureData->materialRect0V;
    constants.materialRect0W = textureData->materialRect0W;
    constants.materialRect0H = textureData->materialRect0H;
    constants.materialRect1U = textureData->materialRect1U;
    constants.materialRect1V = textureData->materialRect1V;
    constants.materialRect1W = textureData->materialRect1W;
    constants.materialRect1H = textureData->materialRect1H;
    constants.materialFlipbook0Cols = textureData->materialFlipbook0Cols;
    constants.materialFlipbook0Rows = textureData->materialFlipbook0Rows;
    constants.materialFlipbook0Frames = textureData->materialFlipbook0Frames;
    constants.materialFlipbook0Fps = textureData->materialFlipbook0Fps;
    constants.materialFlipbook1Cols = textureData->materialFlipbook1Cols;
    constants.materialFlipbook1Rows = textureData->materialFlipbook1Rows;
    constants.materialFlipbook1Frames = textureData->materialFlipbook1Frames;
    constants.materialFlipbook1Fps = textureData->materialFlipbook1Fps;
    return constants;
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

    debugVertexFrameOffset_ = 0;
    worldVertexFrameOffset_ = 0;
    worldIndexFrameOffset_ = 0;
    spriteVertexFrameOffset_ = 0;

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
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
    if (dsvHeap_) {
        dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        dsvHandle.ptr += static_cast<SIZE_T>(frameIndex_) * static_cast<SIZE_T>(dsvDescriptorSize_);
    }

    commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, dsvHeap_ ? &dsvHandle : nullptr);
    commandList_->ClearRenderTargetView(rtvHandle, clearColor_, 0, nullptr);
    if (dsvHeap_) {
        commandList_->ClearDepthStencilView(
            dsvHandle,
            D3D12_CLEAR_FLAG_DEPTH,
            1.0f,
            0,
            0,
            nullptr);
    }

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
    releaseDepthResources();

    if (SUCCEEDED(swapChain_->ResizeBuffers(
            kFrameCount,
            static_cast<UINT>(width_),
            static_cast<UINT>(height_),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            0))) {
        frameIndex_ = swapChain_->GetCurrentBackBufferIndex();
        createRenderTargets();
        createDepthResources();
    }
#else
    (void)width;
    (void)height;
#endif
}

void D3D12RenderBackend::drawWorldTriangles(const WorldTriangle* triangles,
                                            std::size_t triangleCount,
                                            const float* viewProjectionMatrix4x4,
                                            int surfaceWidth,
                                            int surfaceHeight) {
#if defined(_WIN32)
    if (!recording_ || !triangles || triangleCount == 0 || !viewProjectionMatrix4x4) return;
    if (surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!worldPipelineState_ || !worldRootSignature_ || !worldVertexBuffer_ || !commandList_ || !srvHeap_) return;
    if (!worldVertexMappedData_) return;

    const std::size_t safeCount = (triangleCount > kMaxWorldTriangles) ? kMaxWorldTriangles : triangleCount;
    if (safeCount == 0) return;
    const std::size_t vertexCount = safeCount * 3;
    const std::size_t neededBytes = vertexCount * sizeof(WorldVertex);
    const std::size_t writeOffset = alignUp(static_cast<std::size_t>(worldVertexFrameOffset_), 256u);
    if (neededBytes == 0 || writeOffset + neededBytes > worldVertexBufferSize_) return;

    WorldVertex* out = reinterpret_cast<WorldVertex*>(worldVertexMappedData_ + writeOffset);
    for (std::size_t i = 0; i < safeCount; ++i) {
        const WorldTriangle& t = triangles[i];
        const auto resolve = [](float vtx, float fallback) {
            return (vtx >= 0.0f) ? vtx : fallback;
        };
        const std::size_t base = i * 3;
        out[base + 0] = WorldVertex{
            t.x1, t.y1, t.z1,
            0.0f, 0.0f,
            resolve(t.r1, t.r),
            resolve(t.g1, t.g),
            resolve(t.b1, t.b),
            resolve(t.a1, t.a)};
        out[base + 1] = WorldVertex{
            t.x2, t.y2, t.z2,
            0.0f, 0.0f,
            resolve(t.r2, t.r),
            resolve(t.g2, t.g),
            resolve(t.b2, t.b),
            resolve(t.a2, t.a)};
        out[base + 2] = WorldVertex{
            t.x3, t.y3, t.z3,
            0.0f, 0.0f,
            resolve(t.r3, t.r),
            resolve(t.g3, t.g),
            resolve(t.b3, t.b),
            resolve(t.a3, t.a)};
    }
    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(surfaceWidth);
    vp.Height = static_cast<float>(surfaceHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, surfaceWidth, surfaceHeight};

    commandList_->RSSetViewports(1, &vp);
    commandList_->RSSetScissorRects(1, &scissor);
    if (srvHeap_) {
        ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
        commandList_->SetDescriptorHeaps(1, heaps);
    }
    commandList_->SetGraphicsRootSignature(worldRootSignature_.Get());
    commandList_->SetGraphicsRoot32BitConstants(0, 16, viewProjectionMatrix4x4, 0);
    const WorldPsConstants worldPs = makeWorldPsConstants(nullptr, 0.0f);
    commandList_->SetGraphicsRoot32BitConstants(
        1,
        static_cast<UINT>(sizeof(WorldPsConstants) / sizeof(float)),
        &worldPs,
        0);
    if (srvHeap_) {
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
        srvHandle.ptr += static_cast<SIZE_T>(worldFallbackTextureDescriptorIndex_) *
                         static_cast<SIZE_T>(srvDescriptorSize_);
        commandList_->SetGraphicsRootDescriptorTable(2, srvHandle);
    }
    commandList_->SetPipelineState(worldPipelineState_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = worldVertexBufferGpuAddress_ + static_cast<std::uint64_t>(writeOffset);
    vbv.StrideInBytes = worldVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(neededBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->DrawInstanced(static_cast<UINT>(vertexCount), 1, 0, 0);
    worldVertexFrameOffset_ = static_cast<UINT>(writeOffset + neededBytes);
#else
    (void)triangles;
    (void)triangleCount;
    (void)viewProjectionMatrix4x4;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::drawWorldIndexedMesh(const WorldMeshVertex* vertices,
                                              std::size_t vertexCount,
                                              const std::uint32_t* indices,
                                              std::size_t indexCount,
                                              const float* viewProjectionMatrix4x4,
                                              int surfaceWidth,
                                              int surfaceHeight) {
#if defined(_WIN32)
    drawWorldIndexedMeshInternal(
        vertices,
        vertexCount,
        indices,
        indexCount,
        worldFallbackTextureDescriptorIndex_,
        nullptr,
        0.0f,
        viewProjectionMatrix4x4,
        surfaceWidth,
        surfaceHeight);
#else
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    (void)viewProjectionMatrix4x4;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::drawWorldIndexedMeshTextured(const WorldMeshVertex* vertices,
                                                      std::size_t vertexCount,
                                                      const std::uint32_t* indices,
                                                      std::size_t indexCount,
                                                      const WorldTextureData* texture,
                                                      const float* viewProjectionMatrix4x4,
                                                      int surfaceWidth,
                                                      int surfaceHeight) {
#if defined(_WIN32)
    SpriteTexture* worldTex = ensureWorldTexture(texture);
    const std::uint32_t descriptorIndex = worldTex ? worldTex->descriptorIndex : worldFallbackTextureDescriptorIndex_;
    const float useTexture = (worldTex && worldTex->valid) ? 1.0f : 0.0f;
    drawWorldIndexedMeshInternal(
        vertices,
        vertexCount,
        indices,
        indexCount,
        descriptorIndex,
        texture,
        useTexture,
        viewProjectionMatrix4x4,
        surfaceWidth,
        surfaceHeight);
#else
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    (void)texture;
    (void)viewProjectionMatrix4x4;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::drawWorldIndexedMeshInternal(const WorldMeshVertex* vertices,
                                                      std::size_t vertexCount,
                                                      const std::uint32_t* indices,
                                                      std::size_t indexCount,
                                                      std::uint32_t textureDescriptorIndex,
                                                      const WorldTextureData* textureData,
                                                      float useTexture,
                                                      const float* viewProjectionMatrix4x4,
                                                      int surfaceWidth,
                                                      int surfaceHeight) {
#if defined(_WIN32)
    if (!recording_ || !vertices || !indices || vertexCount == 0 || indexCount == 0 || !viewProjectionMatrix4x4) {
        return;
    }
    if (surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!worldPipelineState_ ||
        !worldRootSignature_ ||
        !worldVertexBuffer_ ||
        !worldIndexBuffer_ ||
        !commandList_ ||
        !srvHeap_) {
        return;
    }
    if (!worldVertexMappedData_ || !worldIndexMappedData_) return;

    const std::size_t maxVertexCapacity = worldVertexBufferSize_ / sizeof(WorldVertex);
    const std::size_t maxIndexCapacity = worldIndexBufferSize_ / sizeof(std::uint32_t);
    const std::size_t safeVertexCount = (std::min)(vertexCount, maxVertexCapacity);
    const std::size_t safeIndexCount = (std::min)(indexCount, maxIndexCapacity);
    if (safeVertexCount == 0 || safeIndexCount < 3u) return;

    const std::size_t vertexBytes = safeVertexCount * sizeof(WorldVertex);
    const std::size_t indexBytes = safeIndexCount * sizeof(std::uint32_t);
    if (vertexBytes == 0 || indexBytes == 0) return;
    const std::size_t vertexWriteOffset = alignUp(static_cast<std::size_t>(worldVertexFrameOffset_), 256u);
    const std::size_t indexWriteOffset = alignUp(static_cast<std::size_t>(worldIndexFrameOffset_), 256u);
    if (vertexWriteOffset + vertexBytes > worldVertexBufferSize_ ||
        indexWriteOffset + indexBytes > worldIndexBufferSize_) {
        return;
    }

    std::memcpy(worldVertexMappedData_ + vertexWriteOffset, vertices, vertexBytes);
    auto* outIndices = reinterpret_cast<std::uint32_t*>(worldIndexMappedData_ + indexWriteOffset);
    std::memcpy(outIndices, indices, indexBytes);

    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(surfaceWidth);
    vp.Height = static_cast<float>(surfaceHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, surfaceWidth, surfaceHeight};

    commandList_->RSSetViewports(1, &vp);
    commandList_->RSSetScissorRects(1, &scissor);

    ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
    commandList_->SetDescriptorHeaps(1, heaps);
    commandList_->SetGraphicsRootSignature(worldRootSignature_.Get());
    commandList_->SetGraphicsRoot32BitConstants(0, 16, viewProjectionMatrix4x4, 0);
    const WorldPsConstants worldPs = makeWorldPsConstants(textureData, useTexture);
    commandList_->SetGraphicsRoot32BitConstants(
        1,
        static_cast<UINT>(sizeof(WorldPsConstants) / sizeof(float)),
        &worldPs,
        0);
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    srvHandle.ptr += static_cast<SIZE_T>(textureDescriptorIndex) * static_cast<SIZE_T>(srvDescriptorSize_);
    commandList_->SetGraphicsRootDescriptorTable(2, srvHandle);
    const bool blendMaterial = textureData && textureData->alphaMode == 2u;
    const std::uint8_t blendMode = textureData ? std::min<std::uint8_t>(2u, textureData->blendMode) : 0u;
    ID3D12PipelineState* pso = worldPipelineState_.Get();
    if (blendMaterial) {
        if (blendMode == 1u && worldAdditiveBlendPipelineState_) {
            pso = worldAdditiveBlendPipelineState_.Get();
        } else if (blendMode == 2u && worldPremultipliedBlendPipelineState_) {
            pso = worldPremultipliedBlendPipelineState_.Get();
        } else if (worldBlendPipelineState_) {
            pso = worldBlendPipelineState_.Get();
        }
    }
    commandList_->SetPipelineState(pso);
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = worldVertexBufferGpuAddress_ + static_cast<std::uint64_t>(vertexWriteOffset);
    vbv.StrideInBytes = worldVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(vertexBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);

    D3D12_INDEX_BUFFER_VIEW ibv{};
    ibv.BufferLocation = worldIndexBufferGpuAddress_ + static_cast<std::uint64_t>(indexWriteOffset);
    ibv.Format = DXGI_FORMAT_R32_UINT;
    ibv.SizeInBytes = static_cast<UINT>(indexBytes);
    commandList_->IASetIndexBuffer(&ibv);

    commandList_->DrawIndexedInstanced(static_cast<UINT>(safeIndexCount), 1, 0, 0, 0);
    worldVertexFrameOffset_ = static_cast<UINT>(vertexWriteOffset + vertexBytes);
    worldIndexFrameOffset_ = static_cast<UINT>(indexWriteOffset + indexBytes);
#else
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    (void)textureDescriptorIndex;
    (void)textureData;
    (void)useTexture;
    (void)viewProjectionMatrix4x4;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::drawDebugQuads(const DebugQuad* quads,
                                        std::size_t quadCount,
                                        int surfaceWidth,
                                        int surfaceHeight) {
#if defined(_WIN32)
    if (!recording_ || !quads || quadCount == 0 || surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!debugPipelineState_ || !debugRootSignature_ || !debugVertexBuffer_ || !commandList_) return;
    if (!debugVertexMappedData_) return;

    const std::size_t safeCount = (quadCount > kMaxDebugQuads) ? kMaxDebugQuads : quadCount;
    const std::size_t vertexCount = safeCount * 6;
    const std::size_t neededBytes = vertexCount * sizeof(DebugVertex);
    const std::size_t writeOffset = alignUp(static_cast<std::size_t>(debugVertexFrameOffset_), 256u);
    if (neededBytes == 0 || writeOffset + neededBytes > debugVertexBufferSize_) return;

    static thread_local std::vector<DebugVertex> verts;
    verts.clear();
    verts.reserve(vertexCount);
    for (std::size_t i = 0; i < safeCount; ++i) {
        engine::render::debug::appendQuadAsTriangles(quads[i], verts);
    }
    if (verts.empty()) return;

    DebugVertex* out = reinterpret_cast<DebugVertex*>(debugVertexMappedData_ + writeOffset);
    const float invW = 1.0f / static_cast<float>(surfaceWidth);
    const float invH = 1.0f / static_cast<float>(surfaceHeight);
    for (std::size_t i = 0; i < verts.size(); ++i) {
        const DebugVertex& src = verts[i];
        out[i].x = src.x * invW * 2.0f - 1.0f;
        out[i].y = 1.0f - src.y * invH * 2.0f;
        out[i].r = src.r;
        out[i].g = src.g;
        out[i].b = src.b;
        out[i].a = src.a;
    }
    const std::size_t clampedVertexCount = verts.size();

    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(surfaceWidth);
    vp.Height = static_cast<float>(surfaceHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, surfaceWidth, surfaceHeight};

    commandList_->RSSetViewports(1, &vp);
    commandList_->RSSetScissorRects(1, &scissor);
    commandList_->SetGraphicsRootSignature(debugRootSignature_.Get());
    commandList_->SetPipelineState(debugPipelineState_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = debugVertexBufferGpuAddress_ + writeOffset;
    vbv.StrideInBytes = debugVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(clampedVertexCount * sizeof(DebugVertex));
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->DrawInstanced(static_cast<UINT>(clampedVertexCount), 1, 0, 0);
    debugVertexFrameOffset_ = static_cast<std::uint32_t>(writeOffset + clampedVertexCount * sizeof(DebugVertex));
#else
    (void)quads;
    (void)quadCount;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::drawDebugLines(const DebugLine* lines,
                                        std::size_t lineCount,
                                        int surfaceWidth,
                                        int surfaceHeight) {
#if defined(_WIN32)
    if (!recording_ || !lines || lineCount == 0 || surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!debugPipelineState_ || !debugRootSignature_ || !debugVertexBuffer_ || !commandList_) return;
    if (!debugVertexMappedData_) return;

    const std::size_t safeCount = (lineCount > kMaxDebugLines) ? kMaxDebugLines : lineCount;

    static thread_local std::vector<DebugVertex> verts;
    verts.clear();
    verts.reserve(safeCount * 6);
    for (std::size_t i = 0; i < safeCount; ++i) {
        engine::render::debug::appendLineAsTriangles(lines[i], verts);
    }
    if (verts.empty()) return;

    const std::size_t neededBytes = verts.size() * sizeof(DebugVertex);
    const std::size_t writeOffset = alignUp(static_cast<std::size_t>(debugVertexFrameOffset_), 256u);
    if (neededBytes == 0 || writeOffset + neededBytes > debugVertexBufferSize_) return;

    DebugVertex* out = reinterpret_cast<DebugVertex*>(debugVertexMappedData_ + writeOffset);
    const float invW = 1.0f / static_cast<float>(surfaceWidth);
    const float invH = 1.0f / static_cast<float>(surfaceHeight);
    for (std::size_t i = 0; i < verts.size(); ++i) {
        const DebugVertex& src = verts[i];
        out[i].x = src.x * invW * 2.0f - 1.0f;
        out[i].y = 1.0f - src.y * invH * 2.0f;
        out[i].r = src.r;
        out[i].g = src.g;
        out[i].b = src.b;
        out[i].a = src.a;
    }

    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(surfaceWidth);
    vp.Height = static_cast<float>(surfaceHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, surfaceWidth, surfaceHeight};

    commandList_->RSSetViewports(1, &vp);
    commandList_->RSSetScissorRects(1, &scissor);
    commandList_->SetGraphicsRootSignature(debugRootSignature_.Get());
    commandList_->SetPipelineState(debugPipelineState_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = debugVertexBufferGpuAddress_ + writeOffset;
    vbv.StrideInBytes = debugVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(neededBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->DrawInstanced(static_cast<UINT>(verts.size()), 1, 0, 0);
    debugVertexFrameOffset_ = static_cast<std::uint32_t>(writeOffset + neededBytes);
#else
    (void)lines;
    (void)lineCount;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::drawDebugTriangles(const DebugTriangle* triangles,
                                            std::size_t triangleCount,
                                            int surfaceWidth,
                                            int surfaceHeight) {
#if defined(_WIN32)
    if (!recording_ || !triangles || triangleCount == 0 || surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!debugPipelineState_ || !debugRootSignature_ || !debugVertexBuffer_ || !commandList_) return;
    if (!debugVertexMappedData_) return;

    const std::size_t safeCount = (triangleCount > kMaxDebugTriangles) ? kMaxDebugTriangles : triangleCount;

    static thread_local std::vector<DebugVertex> verts;
    verts.clear();
    verts.reserve(safeCount * 3);
    for (std::size_t i = 0; i < safeCount; ++i) {
        const DebugTriangle& t = triangles[i];
        verts.push_back(DebugVertex{t.x1, t.y1, t.r, t.g, t.b, t.a});
        verts.push_back(DebugVertex{t.x2, t.y2, t.r, t.g, t.b, t.a});
        verts.push_back(DebugVertex{t.x3, t.y3, t.r, t.g, t.b, t.a});
    }
    if (verts.empty()) return;

    const std::size_t neededBytes = verts.size() * sizeof(DebugVertex);
    const std::size_t writeOffset = alignUp(static_cast<std::size_t>(debugVertexFrameOffset_), 256u);
    if (neededBytes == 0 || writeOffset + neededBytes > debugVertexBufferSize_) return;

    DebugVertex* out = reinterpret_cast<DebugVertex*>(debugVertexMappedData_ + writeOffset);
    const float invW = 1.0f / static_cast<float>(surfaceWidth);
    const float invH = 1.0f / static_cast<float>(surfaceHeight);
    for (std::size_t i = 0; i < verts.size(); ++i) {
        const DebugVertex& src = verts[i];
        out[i].x = src.x * invW * 2.0f - 1.0f;
        out[i].y = 1.0f - src.y * invH * 2.0f;
        out[i].r = src.r;
        out[i].g = src.g;
        out[i].b = src.b;
        out[i].a = src.a;
    }
    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(surfaceWidth);
    vp.Height = static_cast<float>(surfaceHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, surfaceWidth, surfaceHeight};

    commandList_->RSSetViewports(1, &vp);
    commandList_->RSSetScissorRects(1, &scissor);
    commandList_->SetGraphicsRootSignature(debugRootSignature_.Get());
    commandList_->SetPipelineState(debugPipelineState_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = debugVertexBufferGpuAddress_ + writeOffset;
    vbv.StrideInBytes = debugVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(neededBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->DrawInstanced(static_cast<UINT>(verts.size()), 1, 0, 0);
    debugVertexFrameOffset_ = static_cast<std::uint32_t>(writeOffset + neededBytes);
#else
    (void)triangles;
    (void)triangleCount;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::drawDebugSprites(const DebugSprite* sprites,
                                          std::size_t spriteCount,
                                          int surfaceWidth,
                                          int surfaceHeight) {
#if defined(_WIN32)
    if (!recording_ || !sprites || spriteCount == 0 || surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!spritePipelineState_ || !spriteRootSignature_ || !spriteVertexBuffer_ || !commandList_ || !srvHeap_) return;
    if (!spriteVertexMappedData_) return;

    const std::size_t safeCount = (spriteCount > kMaxSpriteQuads) ? kMaxSpriteQuads : spriteCount;

    static thread_local std::vector<SpriteVertex> vertices;
    vertices.clear();
    vertices.reserve(safeCount * 6);
    static thread_local std::vector<std::uint32_t> descriptorIndices;
    descriptorIndices.clear();
    descriptorIndices.reserve(safeCount);

    for (std::size_t i = 0; i < safeCount; ++i) {
        const DebugSprite& sprite = sprites[i];
        if (sprite.w <= 0.0f || sprite.h <= 0.0f) continue;

        SpriteTexture* texture = ensureSpriteTexture(sprite.texturePath);
        if (!texture || !texture->valid) continue;

        const float x0 = sprite.x;
        const float y0 = sprite.y;
        const float x1 = sprite.x + sprite.w;
        const float y1 = sprite.y + sprite.h;
        const float r = sprite.r;
        const float g = sprite.g;
        const float b = sprite.b;
        const float a = sprite.a;
        const float u0 = sprite.u0;
        const float v0 = sprite.v0;
        const float u1 = sprite.u1;
        const float v1 = sprite.v1;

        vertices.push_back({x0, y0, u0, v0, r, g, b, a});
        vertices.push_back({x1, y0, u1, v0, r, g, b, a});
        vertices.push_back({x1, y1, u1, v1, r, g, b, a});
        vertices.push_back({x0, y0, u0, v0, r, g, b, a});
        vertices.push_back({x1, y1, u1, v1, r, g, b, a});
        vertices.push_back({x0, y1, u0, v1, r, g, b, a});
        descriptorIndices.push_back(texture->descriptorIndex);
    }
    if (vertices.empty() || descriptorIndices.empty()) return;

    const std::size_t neededBytes = vertices.size() * sizeof(SpriteVertex);
    const std::size_t writeOffset = alignUp(static_cast<std::size_t>(spriteVertexFrameOffset_), 256u);
    if (neededBytes == 0 || writeOffset + neededBytes > spriteVertexBufferSize_) return;

    std::memcpy(spriteVertexMappedData_ + writeOffset, vertices.data(), neededBytes);

    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(surfaceWidth);
    vp.Height = static_cast<float>(surfaceHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, surfaceWidth, surfaceHeight};

    commandList_->RSSetViewports(1, &vp);
    commandList_->RSSetScissorRects(1, &scissor);
    ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
    commandList_->SetDescriptorHeaps(1, heaps);
    commandList_->SetGraphicsRootSignature(spriteRootSignature_.Get());
    const float surfaceSize[2] = {
        static_cast<float>(surfaceWidth),
        static_cast<float>(surfaceHeight)
    };
    commandList_->SetGraphicsRoot32BitConstants(0, 2, surfaceSize, 0);
    commandList_->SetPipelineState(spritePipelineState_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = spriteVertexBufferGpuAddress_ + writeOffset;
    vbv.StrideInBytes = spriteVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(neededBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);

    D3D12_GPU_DESCRIPTOR_HANDLE baseSrv = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    for (std::size_t i = 0; i < descriptorIndices.size(); ++i) {
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = baseSrv;
        srvHandle.ptr += static_cast<SIZE_T>(descriptorIndices[i]) * static_cast<SIZE_T>(srvDescriptorSize_);
        commandList_->SetGraphicsRootDescriptorTable(1, srvHandle);
        commandList_->DrawInstanced(6, 1, static_cast<UINT>(i * 6), 0);
    }
    spriteVertexFrameOffset_ = static_cast<std::uint32_t>(writeOffset + neededBytes);
#else
    (void)sprites;
    (void)spriteCount;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::shutdown() {
#if defined(_WIN32)
    if (!initialized_) return;

    waitForGpu();
    releaseRenderTargets();
    releaseDepthResources();
    if (debugVertexBuffer_ && debugVertexMappedData_) {
        D3D12_RANGE readRange{0, 0};
        debugVertexBuffer_->Unmap(0, &readRange);
    }
    debugVertexMappedData_ = nullptr;
    debugVertexBuffer_.Reset();
    debugVertexBufferGpuAddress_ = 0;
    debugVertexStride_ = 0;
    debugVertexBufferSize_ = 0;
    debugVertexFrameOffset_ = 0;
    if (worldVertexBuffer_ && worldVertexMappedData_) {
        D3D12_RANGE readRange{0, 0};
        worldVertexBuffer_->Unmap(0, &readRange);
    }
    worldVertexMappedData_ = nullptr;
    worldVertexBuffer_.Reset();
    worldVertexBufferGpuAddress_ = 0;
    worldVertexStride_ = 0;
    worldVertexBufferSize_ = 0;
    worldVertexFrameOffset_ = 0;
    if (worldIndexBuffer_ && worldIndexMappedData_) {
        D3D12_RANGE readRange{0, 0};
        worldIndexBuffer_->Unmap(0, &readRange);
    }
    worldIndexMappedData_ = nullptr;
    worldIndexBuffer_.Reset();
    worldIndexBufferGpuAddress_ = 0;
    worldIndexBufferSize_ = 0;
    worldIndexFrameOffset_ = 0;
    worldFallbackTextureDescriptorIndex_ = 0;
    worldPremultipliedBlendPipelineState_.Reset();
    worldAdditiveBlendPipelineState_.Reset();
    worldBlendPipelineState_.Reset();
    worldPipelineState_.Reset();
    worldRootSignature_.Reset();
    spriteTextures_.clear();
    worldTextures_.clear();
    if (spriteVertexBuffer_ && spriteVertexMappedData_) {
        D3D12_RANGE readRange{0, 0};
        spriteVertexBuffer_->Unmap(0, &readRange);
    }
    spriteVertexMappedData_ = nullptr;
    spriteVertexBuffer_.Reset();
    spriteVertexBufferGpuAddress_ = 0;
    spriteVertexStride_ = 0;
    spriteVertexBufferSize_ = 0;
    spriteVertexFrameOffset_ = 0;
    spritePipelineState_.Reset();
    spriteRootSignature_.Reset();
    srvHeap_.Reset();
    srvDescriptorSize_ = 0;
    nextSrvDescriptorIndex_ = 0;
    debugPipelineState_.Reset();
    debugRootSignature_.Reset();

    commandList_.Reset();
    for (auto& allocator : commandAllocators_) allocator.Reset();
    rtvHeap_.Reset();
    dsvHeap_.Reset();
    dsvDescriptorSize_ = 0;
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

    const auto selection = engine::render::dxgi::selectHardwareAdapter(factory_.Get(), preferredAdapterName);
    if (!selection.adapter) {
        throw std::runtime_error("No suitable DXGI adapter found for D3D12 backend.");
    }
    adapter_ = selection.adapter;
    adapterName_ = selection.name.empty() ? "<unnamed dxgi adapter>" : selection.name;
    discreteAdapter_ = selection.discrete;

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

    createDebugPipeline();
    createWorldPipeline();
    createSpritePipeline();
    createRenderTargets();
    createDepthResources();
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

void D3D12RenderBackend::createDepthResources() {
#if defined(_WIN32)
    if (!device_ || width_ <= 0 || height_ <= 0) return;

    if (!dsvHeap_) {
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.NumDescriptors = kFrameCount;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device_->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(dsvHeap_.ReleaseAndGetAddressOf()))) ||
            !dsvHeap_) {
            throw std::runtime_error("CreateDescriptorHeap (DSV) failed.");
        }
        dsvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    }

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC depthDesc{};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Alignment = 0;
    depthDesc.Width = static_cast<UINT64>(width_);
    depthDesc.Height = static_cast<UINT>(height_);
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t i = 0; i < kFrameCount; ++i) {
        depthBuffers_[i].Reset();
        if (FAILED(device_->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &depthDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &clearValue,
                IID_PPV_ARGS(depthBuffers_[i].ReleaseAndGetAddressOf()))) ||
            !depthBuffers_[i]) {
            throw std::runtime_error("CreateCommittedResource failed for D3D12 depth buffer.");
        }
        device_->CreateDepthStencilView(depthBuffers_[i].Get(), &dsvDesc, dsvHandle);
        dsvHandle.ptr += static_cast<SIZE_T>(dsvDescriptorSize_);
    }
#endif
}

void D3D12RenderBackend::releaseDepthResources() {
#if defined(_WIN32)
    for (auto& depth : depthBuffers_) {
        depth.Reset();
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

void D3D12RenderBackend::createDebugPipeline() {
#if defined(_WIN32)
    static constexpr char kVsSource[] =
        "struct VSIn { float2 pos : POSITION; float4 col : COLOR; };"
        "struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR; };"
        "VSOut main(VSIn i) { VSOut o; o.pos = float4(i.pos, 0.0, 1.0); o.col = i.col; return o; }";
    static constexpr char kPsSource[] =
        "struct PSIn { float4 pos : SV_POSITION; float4 col : COLOR; };"
        "float4 main(PSIn i) : SV_TARGET { return i.col; }";

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    if (FAILED(D3DCompile(kVsSource, sizeof(kVsSource) - 1, nullptr, nullptr, nullptr,
                          "main", "vs_5_0", 0, 0, vsBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !vsBlob) {
        throw std::runtime_error("D3DCompile failed for debug VS.");
    }
    errBlob.Reset();
    if (FAILED(D3DCompile(kPsSource, sizeof(kPsSource) - 1, nullptr, nullptr, nullptr,
                          "main", "ps_5_0", 0, 0, psBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !psBlob) {
        throw std::runtime_error("D3DCompile failed for debug PS.");
    }

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    Microsoft::WRL::ComPtr<ID3DBlob> serializedRs;
    Microsoft::WRL::ComPtr<ID3DBlob> rsErr;
    if (FAILED(D3D12SerializeRootSignature(&rsDesc,
                                           D3D_ROOT_SIGNATURE_VERSION_1,
                                           serializedRs.ReleaseAndGetAddressOf(),
                                           rsErr.ReleaseAndGetAddressOf())) ||
        !serializedRs) {
        throw std::runtime_error("D3D12SerializeRootSignature failed.");
    }
    if (FAILED(device_->CreateRootSignature(0,
                                            serializedRs->GetBufferPointer(),
                                            serializedRs->GetBufferSize(),
                                            IID_PPV_ARGS(debugRootSignature_.ReleaseAndGetAddressOf()))) ||
        !debugRootSignature_) {
        throw std::runtime_error("CreateRootSignature failed for D3D12 debug pipeline.");
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = debugRootSignature_.Get();
    pso.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    pso.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};

    D3D12_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC rtBlend{};
    rtBlend.BlendEnable = TRUE;
    rtBlend.LogicOpEnable = FALSE;
    rtBlend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rtBlend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0] = rtBlend;
    pso.BlendState = blend;

    pso.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC raster{};
    raster.FillMode = D3D12_FILL_MODE_SOLID;
    raster.CullMode = D3D12_CULL_MODE_NONE;
    raster.FrontCounterClockwise = FALSE;
    raster.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    raster.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    raster.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    raster.DepthClipEnable = TRUE;
    raster.MultisampleEnable = FALSE;
    raster.AntialiasedLineEnable = FALSE;
    raster.ForcedSampleCount = 0;
    raster.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pso.RasterizerState = raster;

    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    D3D12_DEPTH_STENCIL_DESC depthStencil{};
    depthStencil.DepthEnable = FALSE;
    depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    depthStencil.StencilEnable = FALSE;
    depthStencil.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    depthStencil.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    depthStencil.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    depthStencil.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    depthStencil.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    depthStencil.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    depthStencil.BackFace = depthStencil.FrontFace;
    pso.DepthStencilState = depthStencil;

    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.InputLayout = {layout, static_cast<UINT>(_countof(layout))};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    if (FAILED(device_->CreateGraphicsPipelineState(&pso,
                                                    IID_PPV_ARGS(debugPipelineState_.ReleaseAndGetAddressOf()))) ||
        !debugPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 debug pipeline.");
    }

    constexpr std::size_t kBufferBytes = kMaxDebugVertices * sizeof(DebugVertex);
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = kBufferBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                D3D12_HEAP_FLAG_NONE,
                                                &bufferDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(debugVertexBuffer_.ReleaseAndGetAddressOf()))) ||
        !debugVertexBuffer_) {
        throw std::runtime_error("CreateCommittedResource failed for D3D12 debug vertex buffer.");
    }

    debugVertexBufferGpuAddress_ = debugVertexBuffer_->GetGPUVirtualAddress();
    debugVertexStride_ = sizeof(DebugVertex);
    debugVertexBufferSize_ = static_cast<UINT>(kBufferBytes);
    debugVertexMappedData_ = nullptr;
    void* debugMapped = nullptr;
    D3D12_RANGE debugReadRange{0, 0};
    if (FAILED(debugVertexBuffer_->Map(0, &debugReadRange, &debugMapped)) || !debugMapped) {
        throw std::runtime_error("Map failed for D3D12 debug vertex buffer.");
    }
    debugVertexMappedData_ = static_cast<std::uint8_t*>(debugMapped);
#endif
}

void D3D12RenderBackend::createWorldPipeline() {
#if defined(_WIN32)
    static constexpr char kVsSource[] =
        "cbuffer VSConstants : register(b0) { float4x4 uViewProj; };"
        "struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };"
        "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };"
        "VSOut main(VSIn i) {"
        "  VSOut o;"
        "  float4 clip = mul(uViewProj, float4(i.pos, 1.0f));"
        "  clip.z = clip.z * 0.5f + clip.w * 0.5f;"
        "  o.pos = clip;"
        "  o.uv = i.uv;"
        "  o.col = i.col;"
        "  return o;"
        "}";
    static constexpr char kPsSource[] = R"HLSL(
cbuffer PSConstants : register(b1) {
  float uUseTexture;
  float uWrapS;
  float uWrapT;
  float uAlphaMode;
  float uAlphaCutoff;
  float uMaterialMode;
  float uMaterialTimeSec;
  float uMaterialFlags;
  float uMaterialAtlasWidth;
  float uMaterialAtlasHeight;
  float uMaterialRect0U;
  float uMaterialRect0V;
  float uMaterialRect0W;
  float uMaterialRect0H;
  float uMaterialRect1U;
  float uMaterialRect1V;
  float uMaterialRect1W;
  float uMaterialRect1H;
  float uMaterialFlipbook0Cols;
  float uMaterialFlipbook0Rows;
  float uMaterialFlipbook0Frames;
  float uMaterialFlipbook0Fps;
  float uMaterialFlipbook1Cols;
  float uMaterialFlipbook1Rows;
  float uMaterialFlipbook1Frames;
  float uMaterialFlipbook1Fps;
};
Texture2D gTex : register(t0);
SamplerState gSampCC : register(s0);
SamplerState gSampRR : register(s1);
SamplerState gSampCR : register(s2);
SamplerState gSampRC : register(s3);
SamplerState gSampMR : register(s4);
SamplerState gSampRM : register(s5);
SamplerState gSampMM : register(s6);
SamplerState gSampCM : register(s7);
SamplerState gSampMC : register(s8);
struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };

float applyWrap(float coord, float mode) {
  if (abs(mode - 33071.0f) < 0.5f) return saturate(coord);
  if (abs(mode - 33648.0f) < 0.5f) {
    float i = floor(coord);
    float f = frac(coord);
    float odd = fmod(abs(i), 2.0f);
    return (odd >= 1.0f) ? (1.0f - f) : f;
  }
  return frac(coord);
}
float2 clampWrappedUvToTexelCenter(float2 uv) {
  uint w = 1, h = 1;
  gTex.GetDimensions(w, h);
  float2 texSize = max(float2((float)w, (float)h), float2(1.0f, 1.0f));
  float2 halfTexel = 0.5f / texSize;
  return clamp(uv, halfTexel, 1.0f.xx - halfTexel);
}
bool isClampWrap(float mode) { return abs(mode - 33071.0f) < 0.5f; }
bool isMirrorWrap(float mode) { return abs(mode - 33648.0f) < 0.5f; }
float4 sampleWorldTextureWithWrap(float2 uv, float2 uvDx, float2 uvDy) {
  bool sClamp = isClampWrap(uWrapS);
  bool tClamp = isClampWrap(uWrapT);
  bool sMirror = isMirrorWrap(uWrapS);
  bool tMirror = isMirrorWrap(uWrapT);

  if (sClamp && tClamp) return gTex.SampleGrad(gSampCC, uv, uvDx, uvDy);
  if (!sClamp && !sMirror && !tClamp && !tMirror) return gTex.SampleGrad(gSampRR, uv, uvDx, uvDy);
  if (sClamp && !tClamp && !tMirror) return gTex.SampleGrad(gSampCR, uv, uvDx, uvDy);
  if (!sClamp && !sMirror && tClamp) return gTex.SampleGrad(gSampRC, uv, uvDx, uvDy);
  if (sMirror && !tClamp && !tMirror) return gTex.SampleGrad(gSampMR, uv, uvDx, uvDy);
  if (!sClamp && !sMirror && tMirror) return gTex.SampleGrad(gSampRM, uv, uvDx, uvDy);
  if (sMirror && tMirror) return gTex.SampleGrad(gSampMM, uv, uvDx, uvDy);
  if (sClamp && tMirror) return gTex.SampleGrad(gSampCM, uv, uvDx, uvDy);
  if (sMirror && tClamp) return gTex.SampleGrad(gSampMC, uv, uvDx, uvDy);
  return gTex.SampleGrad(gSampRR, uv, uvDx, uvDy);
}

float hash11(float x) { return frac(sin(x * 12.9898f) * 43758.5453f); }
float hash21(float2 p) {
  float n = dot(p, float2(127.1f, 311.7f));
  return frac(sin(n) * 43758.5453f);
}
float valueNoise2D(float2 p) {
  float2 i = floor(p);
  float2 f = frac(p);
  float2 u = f * f * (3.0f - 2.0f * f);
  float a = hash21(i);
  float b = hash21(i + float2(1.0f, 0.0f));
  float c = hash21(i + float2(0.0f, 1.0f));
  float d = hash21(i + float2(1.0f, 1.0f));
  return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
float smoothFlicker(float t, float seed) {
  float x = t * 9.0f + seed * 97.0f;
  float i = floor(x);
  float f = frac(x);
  f = f * f * (3.0f - 2.0f * f);
  return lerp(hash11(i), hash11(i + 1.0f), f);
}
float fbm2D(float2 p) {
  float v = 0.0f;
  float a = 0.5f;
  [unroll]
  for (int k = 0; k < 5; ++k) {
    v += a * valueNoise2D(p);
    p *= 2.02f;
    a *= 0.5f;
  }
  return v;
}
float2 fbmGrad(float2 p) {
  float e = 0.03f;
  float nx = fbm2D(p + float2(e, 0.0f)) - fbm2D(p - float2(e, 0.0f));
  float ny = fbm2D(p + float2(0.0f, e)) - fbm2D(p - float2(0.0f, e));
  return float2(nx, ny) / (2.0f * e);
}
float2 curl2D(float2 p) {
  float2 g = fbmGrad(p);
  return float2(g.y, -g.x);
}
float2 advect2D(float2 p, float flowY, float amount) {
  float2 c1 = curl2D(p * 1.30f + float2(0.0f, -flowY * 0.10f));
  float2 c2 = curl2D(p * 2.70f + float2(3.1f, -flowY * 0.18f));
  return p + (c1 * 0.65f + c2 * 0.35f) * amount;
}
float3 tonemapSoftLocal(float3 c) { return c / (1.0f + c); }

float2 clampUvToRegionPixels(float2 localUV01, float4 rectUv) {
  float2 atlasSize = max(float2(uMaterialAtlasWidth, uMaterialAtlasHeight), float2(1.0f, 1.0f));
  float2 rectPx = max(rectUv.zw * atlasSize, float2(1.0f, 1.0f));
  float2 minPx = float2(0.5f, 0.5f) / atlasSize;
  float2 maxPx = (rectPx - float2(0.5f, 0.5f)) / atlasSize;
  float2 uv = saturate(localUV01);
  float2 regionUv = rectUv.xy + uv * rectUv.zw;
  return rectUv.xy + clamp(regionUv - rectUv.xy, minPx, maxPx);
}

float4 sampleAtlasCombined(float4 rectUv, float2 grid, float frames, float fps, float2 localUV01, float seed, float t) {
  float speed = lerp(0.85f, 1.10f, hash11(seed * 31.7f + 2.3f));
  float f = floor(t * fps * speed + seed * frames);
  float frame = fmod(f, max(1.0f, frames));
  if (frame < 0.0f) frame += max(1.0f, frames);
  float cols = max(1.0f, grid.x);
  float rows = max(1.0f, grid.y);
  float col = fmod(frame, cols);
  float rowFromTop = floor(frame / cols);
  float row = (rows - 1.0f) - rowFromTop;
  float2 cellUVLocal = (float2(col, row) + localUV01) / float2(cols, rows);
  float2 cellUv = clampUvToRegionPixels(cellUVLocal, rectUv);
  return gTex.Sample(gSampCC, cellUv);
}

float lickBlobs(float x, float y, float2 advP, float flowY, float seed) {
  float k = y * 6.6f + flowY * 0.55f;
  float seg = floor(k);
  float f = frac(k);
  float cx1 = (hash11(seg + seed * 31.0f) - 0.5f) * 0.95f * (1.0f - y);
  float cx2 = (hash11(seg + seed * 73.0f) - 0.5f) * 0.95f * (1.0f - y);
  float w = lerp(0.34f, 0.085f, y);
  float2 q1 = float2((x - cx1) / w,        (f - 0.30f) / 0.70f);
  float2 q2 = float2((x - cx2) / (w*0.85f),(f - 0.45f) / 0.65f);
  float m1 = 1.0f - smoothstep(0.60f, 1.00f, length(q1 * float2(1.0f, 1.45f)));
  float m2 = 1.0f - smoothstep(0.60f, 1.00f, length(q2 * float2(1.0f, 1.60f)));
  float br = fbm2D(advP * float2(7.0f, 12.0f) + seed * 17.0f);
  float broken = smoothstep(0.25f, 0.88f, br);
  float gate = smoothstep(0.05f, 0.22f, y) * (1.0f - smoothstep(0.86f, 1.0f, y));
  float m = (m1 + 0.85f * m2) * broken * gate;
  return saturate(m);
}

float4 evalFireTailExact(PSIn i) {
  float age = saturate(i.col.r);
  float vSeed = saturate(i.col.g);
  float t = uMaterialTimeSec;
  // Legacy fire_tail.frag flips gl_PointCoord.y; shared quads already provide the legacy-facing orientation.
  float2 uv = i.uv;
  float2 cc = (uv - 0.5f) * 2.0f;
  float x = cc.x;
  float y = saturate(uv.y);
  float bottomFade = smoothstep(0.00f, 0.11f, y);

  float baseT = smoothstep(0.00f, 0.22f, y);
  float xScaleBase = lerp(2.55f, 1.90f, baseT);
  float yScaleBase = lerp(1.05f, 0.75f, baseT);
  float reBase = length(float2(cc.x * xScaleBase, cc.y * yScaleBase));
  float radialMaskBase = 1.0f - smoothstep(0.98f, 1.10f, reBase);
  float tightMask = 1.0f - smoothstep(0.62f, 0.88f, reBase);
  float reLoose = length(cc * float2(0.55f, 0.85f));
  float radialMaskLoose = 1.0f - smoothstep(0.98f, 1.20f, reLoose);

  float fade = (1.0f - age);
  fade = pow(lerp(fade, 1.0f, 0.25f), 0.75f);

  float2 wobble = float2(
    smoothFlicker(t * 0.9f, vSeed + 0.17f),
    smoothFlicker(t * 1.1f, vSeed + 0.73f)
  ) - 0.5f;
  float2 local1 = uv + wobble * 0.010f;
  float2 local2 = uv + wobble * 0.002f;

  float4 fb1 = float4(1,1,1,1);
  float4 fb2 = float4(1,1,1,1);
  bool has1 = (uMaterialFlags >= 0.5f);
  bool has2 = (uMaterialFlags >= 2.5f);
  if (has1) {
    fb1 = sampleAtlasCombined(float4(uMaterialRect0U, uMaterialRect0V, uMaterialRect0W, uMaterialRect0H),
                              float2(uMaterialFlipbook0Cols, uMaterialFlipbook0Rows),
                              uMaterialFlipbook0Frames, uMaterialFlipbook0Fps, local1, vSeed, t);
    if (has2) {
      fb2 = sampleAtlasCombined(float4(uMaterialRect1U, uMaterialRect1V, uMaterialRect1W, uMaterialRect1H),
                                float2(uMaterialFlipbook1Cols, uMaterialFlipbook1Rows),
                                uMaterialFlipbook1Frames, uMaterialFlipbook1Fps, local2, vSeed, t);
    } else {
      fb2 = fb1;
    }
  }

  float fb1A = saturate(fb1.a);
  float fb1Lum = saturate(dot(fb1.rgb, float3(0.3333f, 0.3333f, 0.3333f)));
  float speed = lerp(0.95f, 1.10f, hash11(vSeed * 19.31f));
  float flow = t * 1.55f * speed;
  float flowY = flow * lerp(0.75f, 1.55f, y * y);
  float width = lerp(0.30f, 0.055f, pow(y, 2.35f));
  float widthHybrid = width * 2.80f;
  float yy = (y * 2.0f - 1.0f);
  yy = yy * 1.45f + 0.38f;
  yy /= 1.12f;
  float2 p = float2(x / widthHybrid, yy) * 1.22f;
  float sway = fbm2D(float2(x * 1.7f, y * 3.8f) + float2(0.0f, -flowY * 0.65f) + vSeed * 7.0f);
  p.x += (sway - 0.5f) * 0.015f * (1.0f - y);
  float d0 = length(p);
  float2 advP = advect2D(p * float2(1.20f, 1.0f) + vSeed * 6.0f, flowY, 0.25f);
  float n = fbm2D(advP * float2(2.7f, 4.5f) + vSeed * 11.0f);
  float d = d0 + (n - 0.5f) * 0.18f * (1.0f - y);
  float core = saturate(1.0f - smoothstep(0.00f, 0.88f, d));
  float outer = saturate(1.0f - smoothstep(0.30f, 1.05f, d));
  float blobs = lickBlobs(x, y, advP, flowY, vSeed);
  float body = saturate(smoothstep(0.92f, 0.12f, d));
  float procAlpha = body * (0.60f + 0.55f * blobs);
  procAlpha *= (0.92f + 0.15f * smoothFlicker(t * 1.2f, vSeed));
  procAlpha *= bottomFade;
  procAlpha *= fade;
  procAlpha = 1.0f - exp(-procAlpha * 1.85f);
  procAlpha = clamp(procAlpha, 0.0f, 0.96f);

  float3 yellow = float3(1.70f, 1.20f, 0.28f);
  float3 red = float3(1.45f, 0.18f, 0.06f);
  float3 orange = float3(1.60f, 0.55f, 0.12f);
  float wave = 0.5f + 0.5f * sin((x * 1.8f + y * 8.5f - flowY * 4.9f) + vSeed * 7.0f);
  float kk = y * 6.0f - flowY * 0.55f;
  float seg = floor(kk);
  float segRand = hash11(seg + vSeed * 71.3f);
  float segRand2 = hash11(seg + vSeed * 19.7f + 5.0f);
  float tri1 = abs(frac((x * 0.85f + y * 1.05f - flowY * 0.18f) * 2.8f + vSeed * 7.0f) - 0.5f) * 2.0f;
  float tri2 = abs(frac((x * 1.10f - y * 0.60f - flowY * 0.14f) * 3.8f + vSeed * 3.0f) - 0.5f) * 2.0f;
  float zig = lerp(tri1, tri2, 0.50f + 0.50f * (segRand - 0.5f));
  zig = smoothstep(0.15f, 0.85f, zig);
  float warp = fbm2D(advect2D(float2(x * 0.85f, y * 1.2f) + vSeed * 6.0f, flowY, 0.22f) * float2(4.5f, 7.5f)) - 0.5f;
  float jag = 0.0f;
  jag += (segRand - 0.5f) * 0.10f;
  jag += (segRand2 - 0.5f) * 0.05f;
  jag += (zig - 0.5f) * 0.14f;
  jag += warp * 0.06f;
  jag *= (1.0f - 0.55f * smoothstep(0.65f, 1.0f, y));
  float boundary = clamp(0.34f + jag, 0.14f, 0.62f);
  float redMask = smoothstep(boundary, boundary + 0.11f, y);
  float3 procRgb = lerp(yellow, red, redMask);
  float band = smoothstep(boundary - 0.02f, boundary + 0.02f, y) *
               (1.0f - smoothstep(boundary + 0.02f, boundary + 0.10f, y));
  procRgb = lerp(procRgb, orange, 0.55f * band);
  float climb = core * (1.0f - smoothstep(0.55f, 0.95f, y)) * (0.35f + 0.65f * wave);
  procRgb = lerp(procRgb, yellow, 0.18f * climb);
  procRgb *= (1.18f + 0.35f * outer);

  float3 hybridRgb = procRgb;
  float hybridAlpha = procAlpha;
  if (has1) {
    hybridAlpha = clamp(hybridAlpha * lerp(0.55f, 1.65f, fb1A), 0.0f, 0.96f);
    hybridRgb *= lerp(0.85f, 1.25f, fb1Lum);
    hybridRgb *= lerp(float3(1.0f,1.0f,1.0f), fb1.rgb * 1.35f, 0.30f);
  }

  float3 fb2Rgb = fb2.rgb;
  float fb2Alpha = pow(saturate(fb2.a), 0.66f);
  float hot = smoothstep(0.10f, 0.55f, 1.0f - y);
  float3 tint = lerp(red, yellow, hot);
  fb2Rgb *= tint * 1.30f;
  fb2Alpha *= tightMask;
  fb2Alpha *= bottomFade;

  float hybridMaskedA = hybridAlpha * radialMaskLoose * bottomFade;
  float fb2MaskedA = fb2Alpha * radialMaskBase;
  float3 rgb = lerp(hybridRgb, fb2Rgb, 0.50f);
  float alpha = lerp(hybridMaskedA, fb2MaskedA, 0.50f);
  alpha *= fade;
  alpha = clamp(alpha + 0.10f * outer * fade, 0.0f, 0.985f);
  rgb *= 2.60f;
  float emissive = (0.85f * outer + 0.45f * core) * fade;
  rgb *= (1.0f + 2.10f * emissive);
  rgb = tonemapSoftLocal(rgb);
  if (alpha < 0.003f) discard;
  rgb *= alpha;
  return float4(rgb, alpha);
}

float4 main(PSIn i) : SV_TARGET {
  if (uMaterialMode > 0.5f) {
    return evalFireTailExact(i);
  }
  float4 tex = float4(1.0f, 1.0f, 1.0f, 1.0f);
  float3 outSrgb = saturate(i.col.rgb);
  if (uUseTexture > 0.5f) {
    float2 uvDx = ddx(i.uv);
    float2 uvDy = ddy(i.uv);
    tex = sampleWorldTextureWithWrap(i.uv, uvDx, uvDy);
    outSrgb = saturate(tex.rgb * i.col.rgb);
  }
  float outA = saturate(i.col.a * tex.a);
  if (uAlphaMode < 0.5f) {
    outA = saturate(i.col.a);
  } else if (uAlphaMode < 1.5f) {
    if (outA < saturate(uAlphaCutoff)) discard;
    outA = saturate(i.col.a);
  }
  return float4(outSrgb, outA);
}
)HLSL";

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    if (FAILED(D3DCompile(kVsSource, sizeof(kVsSource) - 1, nullptr, nullptr, nullptr,
                          "main", "vs_5_0", 0, 0, vsBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !vsBlob) {
        throw std::runtime_error("D3DCompile failed for world VS.");
    }
    errBlob.Reset();
    if (FAILED(D3DCompile(kPsSource, sizeof(kPsSource) - 1, nullptr, nullptr, nullptr,
                          "main", "ps_5_0", 0, 0, psBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !psBlob) {
        throw std::runtime_error("D3DCompile failed for world PS.");
    }

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[3]{};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.Num32BitValues = 16;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[1].Constants.Num32BitValues = static_cast<UINT>(sizeof(WorldPsConstants) / sizeof(float));
    rootParams[1].Constants.ShaderRegister = 1;
    rootParams[1].Constants.RegisterSpace = 0;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    auto makeStaticWorldSampler = [](UINT shaderRegister,
                                     D3D12_TEXTURE_ADDRESS_MODE addressU,
                                     D3D12_TEXTURE_ADDRESS_MODE addressV) {
        D3D12_STATIC_SAMPLER_DESC s{};
        s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = addressU;
        s.AddressV = addressV;
        s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.MipLODBias = 0.0f;
        s.MaxAnisotropy = 1;
        s.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        s.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        s.MinLOD = 0.0f;
        s.MaxLOD = D3D12_FLOAT32_MAX;
        s.ShaderRegister = shaderRegister;
        s.RegisterSpace = 0;
        s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        return s;
    };
    std::array<D3D12_STATIC_SAMPLER_DESC, 9> worldSamplers = {
        makeStaticWorldSampler(0, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  D3D12_TEXTURE_ADDRESS_MODE_CLAMP),  // CC
        makeStaticWorldSampler(1, D3D12_TEXTURE_ADDRESS_MODE_WRAP,   D3D12_TEXTURE_ADDRESS_MODE_WRAP),   // RR
        makeStaticWorldSampler(2, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  D3D12_TEXTURE_ADDRESS_MODE_WRAP),   // CR
        makeStaticWorldSampler(3, D3D12_TEXTURE_ADDRESS_MODE_WRAP,   D3D12_TEXTURE_ADDRESS_MODE_CLAMP),  // RC
        makeStaticWorldSampler(4, D3D12_TEXTURE_ADDRESS_MODE_MIRROR, D3D12_TEXTURE_ADDRESS_MODE_WRAP),   // MR
        makeStaticWorldSampler(5, D3D12_TEXTURE_ADDRESS_MODE_WRAP,   D3D12_TEXTURE_ADDRESS_MODE_MIRROR), // RM
        makeStaticWorldSampler(6, D3D12_TEXTURE_ADDRESS_MODE_MIRROR, D3D12_TEXTURE_ADDRESS_MODE_MIRROR), // MM
        makeStaticWorldSampler(7, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  D3D12_TEXTURE_ADDRESS_MODE_MIRROR), // CM
        makeStaticWorldSampler(8, D3D12_TEXTURE_ADDRESS_MODE_MIRROR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),  // MC
    };

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = static_cast<UINT>(_countof(rootParams));
    rsDesc.pParameters = rootParams;
    rsDesc.NumStaticSamplers = static_cast<UINT>(worldSamplers.size());
    rsDesc.pStaticSamplers = worldSamplers.data();
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRs;
    Microsoft::WRL::ComPtr<ID3DBlob> rsErr;
    if (FAILED(D3D12SerializeRootSignature(&rsDesc,
                                           D3D_ROOT_SIGNATURE_VERSION_1,
                                           serializedRs.ReleaseAndGetAddressOf(),
                                           rsErr.ReleaseAndGetAddressOf())) ||
        !serializedRs) {
        throw std::runtime_error("D3D12SerializeRootSignature failed for world pipeline.");
    }
    if (FAILED(device_->CreateRootSignature(0,
                                            serializedRs->GetBufferPointer(),
                                            serializedRs->GetBufferSize(),
                                            IID_PPV_ARGS(worldRootSignature_.ReleaseAndGetAddressOf()))) ||
        !worldRootSignature_) {
        throw std::runtime_error("CreateRootSignature failed for D3D12 world pipeline.");
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = worldRootSignature_.Get();
    pso.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    pso.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};

    D3D12_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC rtBlend{};
    rtBlend.BlendEnable = TRUE;
    rtBlend.LogicOpEnable = FALSE;
    rtBlend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rtBlend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0] = rtBlend;
    pso.BlendState = blend;
    pso.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC raster{};
    raster.FillMode = D3D12_FILL_MODE_SOLID;
    raster.CullMode = D3D12_CULL_MODE_NONE;
    raster.FrontCounterClockwise = FALSE;
    raster.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    raster.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    raster.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    raster.DepthClipEnable = TRUE;
    raster.MultisampleEnable = FALSE;
    raster.AntialiasedLineEnable = FALSE;
    raster.ForcedSampleCount = 0;
    raster.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pso.RasterizerState = raster;

    D3D12_DEPTH_STENCIL_DESC depthStencil{};
    depthStencil.DepthEnable = TRUE;
    depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    depthStencil.StencilEnable = FALSE;
    pso.DepthStencilState = depthStencil;

    pso.InputLayout = {layout, static_cast<UINT>(_countof(layout))};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;
    if (FAILED(device_->CreateGraphicsPipelineState(&pso,
                                                    IID_PPV_ARGS(worldPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world pipeline.");
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC blendPso = pso;
    blendPso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(device_->CreateGraphicsPipelineState(
            &blendPso,
            IID_PPV_ARGS(worldBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldBlendPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world blend pipeline.");
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC additiveBlendPso = blendPso;
    additiveBlendPso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    additiveBlendPso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    additiveBlendPso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    additiveBlendPso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    additiveBlendPso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    additiveBlendPso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    if (FAILED(device_->CreateGraphicsPipelineState(
            &additiveBlendPso,
            IID_PPV_ARGS(worldAdditiveBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldAdditiveBlendPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world additive blend pipeline.");
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC premulBlendPso = blendPso;
    premulBlendPso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    premulBlendPso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    premulBlendPso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    premulBlendPso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    premulBlendPso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    premulBlendPso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    if (FAILED(device_->CreateGraphicsPipelineState(
            &premulBlendPso,
            IID_PPV_ARGS(worldPremultipliedBlendPipelineState_.ReleaseAndGetAddressOf()))) ||
        !worldPremultipliedBlendPipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 world premultiplied blend pipeline.");
    }

    constexpr std::size_t kBufferBytes = kMaxWorldVertices * sizeof(WorldVertex);
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = kBufferBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                D3D12_HEAP_FLAG_NONE,
                                                &bufferDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(worldVertexBuffer_.ReleaseAndGetAddressOf()))) ||
        !worldVertexBuffer_) {
        throw std::runtime_error("CreateCommittedResource failed for D3D12 world vertex buffer.");
    }

    worldVertexBufferGpuAddress_ = worldVertexBuffer_->GetGPUVirtualAddress();
    worldVertexStride_ = sizeof(WorldVertex);
    worldVertexBufferSize_ = static_cast<UINT>(kBufferBytes);
    worldVertexMappedData_ = nullptr;
    void* worldVertexMapped = nullptr;
    D3D12_RANGE worldVertexReadRange{0, 0};
    if (FAILED(worldVertexBuffer_->Map(0, &worldVertexReadRange, &worldVertexMapped)) || !worldVertexMapped) {
        throw std::runtime_error("Map failed for D3D12 world vertex buffer.");
    }
    worldVertexMappedData_ = static_cast<std::uint8_t*>(worldVertexMapped);

    const std::size_t indexBufferBytes = kMaxWorldIndices * sizeof(std::uint32_t);
    D3D12_RESOURCE_DESC indexBufferDesc = bufferDesc;
    indexBufferDesc.Width = indexBufferBytes;
    if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                D3D12_HEAP_FLAG_NONE,
                                                &indexBufferDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(worldIndexBuffer_.ReleaseAndGetAddressOf()))) ||
        !worldIndexBuffer_) {
        throw std::runtime_error("CreateCommittedResource failed for D3D12 world index buffer.");
    }
    worldIndexBufferGpuAddress_ = worldIndexBuffer_->GetGPUVirtualAddress();
    worldIndexBufferSize_ = static_cast<UINT>(indexBufferBytes);
    worldIndexMappedData_ = nullptr;
    void* worldIndexMapped = nullptr;
    D3D12_RANGE worldIndexReadRange{0, 0};
    if (FAILED(worldIndexBuffer_->Map(0, &worldIndexReadRange, &worldIndexMapped)) || !worldIndexMapped) {
        throw std::runtime_error("Map failed for D3D12 world index buffer.");
    }
    worldIndexMappedData_ = static_cast<std::uint8_t*>(worldIndexMapped);
#endif
}

void D3D12RenderBackend::createSpritePipeline() {
#if defined(_WIN32)
    if (!device_) return;

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
    srvHeapDesc.NumDescriptors = static_cast<UINT>(kMaxSrvDescriptors);
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(srvHeap_.ReleaseAndGetAddressOf()))) ||
        !srvHeap_) {
        throw std::runtime_error("CreateDescriptorHeap (SRV) failed for D3D12 sprite pipeline.");
    }
    srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    nextSrvDescriptorIndex_ = 0;
    spriteTextures_.clear();
    worldTextures_.clear();

    static constexpr char kVsSource[] =
        "cbuffer VSConstants : register(b0) { float2 uSurfaceSize; };"
        "struct VSIn { float2 pos : POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };"
        "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };"
        "VSOut main(VSIn i) {"
        "  VSOut o;"
        "  float2 ndc;"
        "  ndc.x = (i.pos.x / max(uSurfaceSize.x, 1.0f)) * 2.0f - 1.0f;"
        "  ndc.y = 1.0f - (i.pos.y / max(uSurfaceSize.y, 1.0f)) * 2.0f;"
        "  o.pos = float4(ndc, 0.0f, 1.0f);"
        "  o.uv = i.uv;"
        "  o.col = i.col;"
        "  return o;"
        "}";
    static constexpr char kPsSource[] =
        "Texture2D gTex : register(t0);"
        "SamplerState gSamp : register(s0);"
        "struct PSIn { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };"
        "float4 main(PSIn i) : SV_TARGET {"
        "  float4 tex = gTex.Sample(gSamp, i.uv);"
        "  return tex * i.col;"
        "}";

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> psBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    if (FAILED(D3DCompile(kVsSource, sizeof(kVsSource) - 1, nullptr, nullptr, nullptr,
                          "main", "vs_5_0", 0, 0, vsBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !vsBlob) {
        throw std::runtime_error("D3DCompile failed for sprite VS.");
    }
    errBlob.Reset();
    if (FAILED(D3DCompile(kPsSource, sizeof(kPsSource) - 1, nullptr, nullptr, nullptr,
                          "main", "ps_5_0", 0, 0, psBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !psBlob) {
        throw std::runtime_error("D3DCompile failed for sprite PS.");
    }

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[2]{};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.Num32BitValues = 2;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_ANISOTROPIC;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = -0.25f;
    sampler.MaxAnisotropy = 16;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = static_cast<UINT>(_countof(rootParams));
    rsDesc.pParameters = rootParams;
    rsDesc.NumStaticSamplers = 1;
    rsDesc.pStaticSamplers = &sampler;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> serializedRs;
    Microsoft::WRL::ComPtr<ID3DBlob> rsErr;
    if (FAILED(D3D12SerializeRootSignature(&rsDesc,
                                           D3D_ROOT_SIGNATURE_VERSION_1,
                                           serializedRs.ReleaseAndGetAddressOf(),
                                           rsErr.ReleaseAndGetAddressOf())) ||
        !serializedRs) {
        throw std::runtime_error("D3D12SerializeRootSignature failed for sprite pipeline.");
    }
    if (FAILED(device_->CreateRootSignature(0,
                                            serializedRs->GetBufferPointer(),
                                            serializedRs->GetBufferSize(),
                                            IID_PPV_ARGS(spriteRootSignature_.ReleaseAndGetAddressOf()))) ||
        !spriteRootSignature_) {
        throw std::runtime_error("CreateRootSignature failed for D3D12 sprite pipeline.");
    }

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = spriteRootSignature_.Get();
    pso.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    pso.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};

    D3D12_BLEND_DESC blend{};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC rtBlend{};
    rtBlend.BlendEnable = TRUE;
    rtBlend.LogicOpEnable = FALSE;
    rtBlend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rtBlend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.RenderTarget[0] = rtBlend;
    pso.BlendState = blend;
    pso.SampleMask = UINT_MAX;

    D3D12_RASTERIZER_DESC raster{};
    raster.FillMode = D3D12_FILL_MODE_SOLID;
    raster.CullMode = D3D12_CULL_MODE_NONE;
    raster.FrontCounterClockwise = FALSE;
    raster.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    raster.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    raster.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    raster.DepthClipEnable = TRUE;
    raster.MultisampleEnable = FALSE;
    raster.AntialiasedLineEnable = FALSE;
    raster.ForcedSampleCount = 0;
    raster.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pso.RasterizerState = raster;

    D3D12_DEPTH_STENCIL_DESC depthStencil{};
    depthStencil.DepthEnable = FALSE;
    depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    depthStencil.StencilEnable = FALSE;
    pso.DepthStencilState = depthStencil;

    pso.InputLayout = {layout, static_cast<UINT>(_countof(layout))};
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    if (FAILED(device_->CreateGraphicsPipelineState(&pso,
                                                    IID_PPV_ARGS(spritePipelineState_.ReleaseAndGetAddressOf()))) ||
        !spritePipelineState_) {
        throw std::runtime_error("CreateGraphicsPipelineState failed for D3D12 sprite pipeline.");
    }

    constexpr std::size_t kBufferBytes = kMaxSpriteVertices * sizeof(SpriteVertex);
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = kBufferBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(device_->CreateCommittedResource(&heapProps,
                                                D3D12_HEAP_FLAG_NONE,
                                                &bufferDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr,
                                                IID_PPV_ARGS(spriteVertexBuffer_.ReleaseAndGetAddressOf()))) ||
        !spriteVertexBuffer_) {
        throw std::runtime_error("CreateCommittedResource failed for D3D12 sprite vertex buffer.");
    }
    spriteVertexBufferGpuAddress_ = spriteVertexBuffer_->GetGPUVirtualAddress();
    spriteVertexStride_ = sizeof(SpriteVertex);
    spriteVertexBufferSize_ = static_cast<UINT>(kBufferBytes);
    spriteVertexMappedData_ = nullptr;
    void* spriteMapped = nullptr;
    D3D12_RANGE spriteReadRange{0, 0};
    if (FAILED(spriteVertexBuffer_->Map(0, &spriteReadRange, &spriteMapped)) || !spriteMapped) {
        throw std::runtime_error("Map failed for D3D12 sprite vertex buffer.");
    }
    spriteVertexMappedData_ = static_cast<std::uint8_t*>(spriteMapped);

    if (SpriteTexture* fallback = ensureFallbackSpriteTexture()) {
        worldFallbackTextureDescriptorIndex_ = fallback->descriptorIndex;
    } else {
        throw std::runtime_error("Failed to create fallback texture for D3D12 world pipeline.");
    }
#endif
}

D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::ensureFallbackSpriteTexture() {
#if defined(_WIN32)
    const auto it = spriteTextures_.find(kFallbackSpriteTextureKey);
    if (it != spriteTextures_.end()) {
        return const_cast<SpriteTexture*>(&it->second);
    }
    if (!device_ || !commandQueue_ || !fence_ || !srvHeap_) return nullptr;
    if (nextSrvDescriptorIndex_ >= kMaxSrvDescriptors) return nullptr;

    static const unsigned char kFallbackRgba[16] = {
         72,  90, 108, 255,
         56,  70,  84, 255,
         56,  70,  84, 255,
         72,  90, 108, 255
    };

    SpriteTexture texture;
    texture.descriptorIndex = nextSrvDescriptorIndex_;
    if (!engine::render::d3d12::createTextureResourceFromRgba(device_.Get(),
                                                              commandQueue_.Get(),
                                                              fence_.Get(),
                                                              static_cast<HANDLE>(fenceEvent_),
                                                              fenceValue_,
                                                              srvHeap_.Get(),
                                                              srvDescriptorSize_,
                                                              texture.descriptorIndex,
                                                              kFallbackRgba,
                                                              2,
                                                              2,
                                                              kGlClampToEdge,
                                                              kGlClampToEdge,
                                                              true,
                                                              texture.resource)) {
        return nullptr;
    }
    texture.valid = true;
    ++nextSrvDescriptorIndex_;
    auto [insertedIt, _] = spriteTextures_.emplace(kFallbackSpriteTextureKey, std::move(texture));
    return &insertedIt->second;
#else
    return nullptr;
#endif
}

D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::ensureSpriteTexture(const std::string& texturePath) {
#if defined(_WIN32)
    if (texturePath.empty()) return ensureFallbackSpriteTexture();

    auto existing = spriteTextures_.find(texturePath);
    if (existing != spriteTextures_.end()) {
        return &existing->second;
    }
    if (!device_ || !commandQueue_ || !fence_ || !srvHeap_) return ensureFallbackSpriteTexture();
    if (nextSrvDescriptorIndex_ >= kMaxSrvDescriptors) return ensureFallbackSpriteTexture();

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* pixels = stbi_load(texturePath.c_str(), &width, &height, &channels, 4);
    std::string altPath;
    if (!pixels) {
        altPath = texturePath;
        std::replace(altPath.begin(), altPath.end(), '\\', '/');
        if (altPath != texturePath) {
            stbi_set_flip_vertically_on_load(false);
            pixels = stbi_load(altPath.c_str(), &width, &height, &channels, 4);
        }
    }

    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        return ensureFallbackSpriteTexture();
    }

    SpriteTexture texture;
    texture.descriptorIndex = nextSrvDescriptorIndex_;
    const bool ok = engine::render::d3d12::createTextureResourceFromRgba(device_.Get(),
                                                                          commandQueue_.Get(),
                                                                          fence_.Get(),
                                                                          static_cast<HANDLE>(fenceEvent_),
                                                                          fenceValue_,
                                                                          srvHeap_.Get(),
                                                                          srvDescriptorSize_,
                                                                          texture.descriptorIndex,
                                                                          pixels,
                                                                          width,
                                                                          height,
                                                                          kGlClampToEdge,
                                                                          kGlClampToEdge,
                                                                          true,
                                                                          texture.resource);
    stbi_image_free(pixels);
    if (!ok) {
        return ensureFallbackSpriteTexture();
    }

    texture.valid = true;
    ++nextSrvDescriptorIndex_;
    auto [insertedIt, _] = spriteTextures_.emplace(texturePath, std::move(texture));
    return &insertedIt->second;
#else
    (void)texturePath;
    return nullptr;
#endif
}

D3D12RenderBackend::SpriteTexture* D3D12RenderBackend::ensureWorldTexture(const WorldTextureData* textureData) {
#if defined(_WIN32)
    if (!textureData ||
        !textureData->rgba ||
        textureData->width <= 0 ||
        textureData->height <= 0 ||
        !textureData->key ||
        textureData->key[0] == '\0') {
        return nullptr;
    }

    const std::string key(textureData->key);
    auto it = worldTextures_.find(key);
    if (it != worldTextures_.end()) {
        return &it->second;
    }

    if (!device_ || !commandQueue_ || !fence_ || !srvHeap_) return nullptr;
    if (nextSrvDescriptorIndex_ >= kMaxSrvDescriptors) return nullptr;

    SpriteTexture texture;
    texture.descriptorIndex = nextSrvDescriptorIndex_;
    const bool ok = engine::render::d3d12::createTextureResourceFromRgba(device_.Get(),
                                                                          commandQueue_.Get(),
                                                                          fence_.Get(),
                                                                          static_cast<HANDLE>(fenceEvent_),
                                                                          fenceValue_,
                                                                          srvHeap_.Get(),
                                                                          srvDescriptorSize_,
                                                                          texture.descriptorIndex,
                                                                          textureData->rgba,
                                                                          textureData->width,
                                                                          textureData->height,
                                                                          textureData->wrapS,
                                                                          textureData->wrapT,
                                                                          false,
                                                                          texture.resource);
    if (!ok) return nullptr;

    texture.valid = true;
    ++nextSrvDescriptorIndex_;
    auto [insertedIt, _] = worldTextures_.emplace(key, std::move(texture));
    return &insertedIt->second;
#else
    (void)textureData;
    return nullptr;
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
