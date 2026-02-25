#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/DxgiAdapterSelection.h"
#include "engine/render/DebugGeometry.h"
#include "engine/render/d3d12/D3D12TextureUpload.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"

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

#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
#endif

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
