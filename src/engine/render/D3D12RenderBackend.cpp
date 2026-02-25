#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/DxgiAdapterSelection.h"
#include "engine/render/DebugGeometry.h"
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

void D3D12RenderBackend::drawWorldIndexedMeshCached(const char* geometryKey,
                                                    const WorldMeshVertex* vertices,
                                                    std::size_t vertexCount,
                                                    const std::uint32_t* indices,
                                                    std::size_t indexCount,
                                                    const float* viewProjectionMatrix4x4,
                                                    int surfaceWidth,
                                                    int surfaceHeight) {
#if defined(_WIN32)
    if (!geometryKey || geometryKey[0] == '\0') {
        drawWorldIndexedMesh(
            vertices,
            vertexCount,
            indices,
            indexCount,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }
    CachedWorldMesh* cached = ensureCachedWorldMesh(geometryKey, vertices, vertexCount, indices, indexCount);
    if (!cached || !cached->valid) {
        drawWorldIndexedMesh(
            vertices,
            vertexCount,
            indices,
            indexCount,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }
    drawWorldIndexedMeshCachedInternal(*cached, viewProjectionMatrix4x4, surfaceWidth, surfaceHeight);
#else
    (void)geometryKey;
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    (void)viewProjectionMatrix4x4;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::prewarmWorldIndexedMeshCached(const char* geometryKey,
                                                       const WorldMeshVertex* vertices,
                                                       std::size_t vertexCount,
                                                       const std::uint32_t* indices,
                                                       std::size_t indexCount) {
#if defined(_WIN32)
    (void)ensureCachedWorldMesh(geometryKey, vertices, vertexCount, indices, indexCount);
#else
    (void)geometryKey;
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
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

// Cache immutable world meshes in dedicated upload buffers so repeated draws
// (e.g. D3D12 shared capture pokeball shake phases) avoid re-memcpying the
// same large vertex/index data through the per-frame dynamic world buffers.
D3D12RenderBackend::CachedWorldMesh* D3D12RenderBackend::ensureCachedWorldMesh(
    const char* geometryKey,
    const WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount) {
#if defined(_WIN32)
    if (!geometryKey || geometryKey[0] == '\0' || !vertices || !indices || vertexCount == 0 || indexCount < 3u) {
        return nullptr;
    }
    if (!device_) return nullptr;

    const std::string key(geometryKey);
    const std::size_t vertexBytes = vertexCount * sizeof(WorldVertex);
    const std::size_t indexBytes = indexCount * sizeof(std::uint32_t);
    if (vertexBytes == 0 || indexBytes == 0) return nullptr;

    auto existing = cachedWorldMeshes_.find(key);
    if (existing != cachedWorldMeshes_.end()) {
        CachedWorldMesh& mesh = existing->second;
        if (mesh.valid &&
            mesh.vertexCount == vertexCount &&
            mesh.indexCount == indexCount &&
            mesh.vertexBytes == vertexBytes &&
            mesh.indexBytes == indexBytes) {
            return &mesh;
        }
        cachedWorldMeshes_.erase(existing);
    }

    const auto createBuffer = [&](D3D12_HEAP_TYPE heapType,
                                  std::size_t sizeBytes,
                                  D3D12_RESOURCE_STATES initialState,
                                  Microsoft::WRL::ComPtr<ID3D12Resource>& outRes) -> bool {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = heapType;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Alignment = 0;
        desc.Width = static_cast<UINT64>(sizeBytes);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        return SUCCEEDED(device_->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            initialState,
            nullptr,
            IID_PPV_ARGS(outRes.ReleaseAndGetAddressOf())));        
    };

    CachedWorldMesh mesh{};
    const bool canUseDefaultHeapCache =
        device_ && commandQueue_ && fence_ && fenceEvent_;

    if (canUseDefaultHeapCache) {
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexUpload;
        Microsoft::WRL::ComPtr<ID3D12Resource> indexUpload;
        if (!createBuffer(
                D3D12_HEAP_TYPE_DEFAULT,
                vertexBytes,
                D3D12_RESOURCE_STATE_COPY_DEST,
                mesh.vertexBuffer) ||
            !mesh.vertexBuffer) {
            return nullptr;
        }
        if (!createBuffer(
                D3D12_HEAP_TYPE_DEFAULT,
                indexBytes,
                D3D12_RESOURCE_STATE_COPY_DEST,
                mesh.indexBuffer) ||
            !mesh.indexBuffer) {
            return nullptr;
        }
        if (!createBuffer(
                D3D12_HEAP_TYPE_UPLOAD,
                vertexBytes,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                vertexUpload) ||
            !vertexUpload) {
            return nullptr;
        }
        if (!createBuffer(
                D3D12_HEAP_TYPE_UPLOAD,
                indexBytes,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                indexUpload) ||
            !indexUpload) {
            return nullptr;
        }

        void* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        if (FAILED(vertexUpload->Map(0, &readRange, &mapped)) || !mapped) return nullptr;
        std::memcpy(mapped, vertices, vertexBytes);
        D3D12_RANGE vertexWriteRange{0, static_cast<SIZE_T>(vertexBytes)};
        vertexUpload->Unmap(0, &vertexWriteRange);

        mapped = nullptr;
        if (FAILED(indexUpload->Map(0, &readRange, &mapped)) || !mapped) return nullptr;
        std::memcpy(mapped, indices, indexBytes);
        D3D12_RANGE indexWriteRange{0, static_cast<SIZE_T>(indexBytes)};
        indexUpload->Unmap(0, &indexWriteRange);

        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> copyAllocator;
        if (FAILED(device_->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(copyAllocator.ReleaseAndGetAddressOf()))) ||
            !copyAllocator) {
            return nullptr;
        }
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> copyList;
        if (FAILED(device_->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                copyAllocator.Get(),
                nullptr,
                IID_PPV_ARGS(copyList.ReleaseAndGetAddressOf()))) ||
            !copyList) {
            return nullptr;
        }

        copyList->CopyBufferRegion(mesh.vertexBuffer.Get(), 0, vertexUpload.Get(), 0, vertexBytes);
        copyList->CopyBufferRegion(mesh.indexBuffer.Get(), 0, indexUpload.Get(), 0, indexBytes);

        D3D12_RESOURCE_BARRIER barriers[2]{};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = mesh.vertexBuffer.Get();
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource = mesh.indexBuffer.Get();
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
        copyList->ResourceBarrier(2, barriers);

        if (FAILED(copyList->Close())) return nullptr;
        ID3D12CommandList* lists[] = {copyList.Get()};
        commandQueue_->ExecuteCommandLists(1, lists);

        const std::uint64_t signalValue = ++fenceValue_;
        if (FAILED(commandQueue_->Signal(fence_.Get(), signalValue))) return nullptr;
        if (fence_->GetCompletedValue() < signalValue) {
            if (FAILED(fence_->SetEventOnCompletion(signalValue, static_cast<HANDLE>(fenceEvent_)))) {
                return nullptr;
            }
            WaitForSingleObject(static_cast<HANDLE>(fenceEvent_), INFINITE);
        }
    } else {
        if (!createBuffer(
                D3D12_HEAP_TYPE_UPLOAD,
                vertexBytes,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                mesh.vertexBuffer) ||
            !mesh.vertexBuffer) {
            return nullptr;
        }
        if (!createBuffer(
                D3D12_HEAP_TYPE_UPLOAD,
                indexBytes,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                mesh.indexBuffer) ||
            !mesh.indexBuffer) {
            return nullptr;
        }

        void* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        if (FAILED(mesh.vertexBuffer->Map(0, &readRange, &mapped)) || !mapped) return nullptr;
        std::memcpy(mapped, vertices, vertexBytes);
        mesh.vertexBuffer->Unmap(0, nullptr);

        mapped = nullptr;
        if (FAILED(mesh.indexBuffer->Map(0, &readRange, &mapped)) || !mapped) return nullptr;
        std::memcpy(mapped, indices, indexBytes);
        mesh.indexBuffer->Unmap(0, nullptr);
    }

    mesh.vertexGpuAddress = mesh.vertexBuffer->GetGPUVirtualAddress();
    mesh.indexGpuAddress = mesh.indexBuffer->GetGPUVirtualAddress();
    mesh.vertexCount = vertexCount;
    mesh.indexCount = indexCount;
    mesh.vertexBytes = vertexBytes;
    mesh.indexBytes = indexBytes;
    mesh.valid = true;

    auto [it, _] = cachedWorldMeshes_.emplace(key, std::move(mesh));
    return &it->second;
#else
    (void)geometryKey;
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    return nullptr;
#endif
}

void D3D12RenderBackend::drawWorldIndexedMeshCachedInternal(const CachedWorldMesh& mesh,
                                                            const float* viewProjectionMatrix4x4,
                                                            int surfaceWidth,
                                                            int surfaceHeight) {
#if defined(_WIN32)
    if (!recording_ || !viewProjectionMatrix4x4) return;
    if (!mesh.valid || !mesh.vertexBuffer || !mesh.indexBuffer || mesh.indexCount < 3u) return;
    if (surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!worldPipelineState_ ||
        !worldRootSignature_ ||
        !commandList_ ||
        !srvHeap_) {
        return;
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

    ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
    commandList_->SetDescriptorHeaps(1, heaps);
    commandList_->SetGraphicsRootSignature(worldRootSignature_.Get());
    commandList_->SetGraphicsRoot32BitConstants(0, 16, viewProjectionMatrix4x4, 0);
    const WorldPsConstants worldPs = makeWorldPsConstants(nullptr, 0.0f);
    commandList_->SetGraphicsRoot32BitConstants(
        1,
        static_cast<UINT>(sizeof(WorldPsConstants) / sizeof(float)),
        &worldPs,
        0);
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    srvHandle.ptr += static_cast<SIZE_T>(worldFallbackTextureDescriptorIndex_) * static_cast<SIZE_T>(srvDescriptorSize_);
    commandList_->SetGraphicsRootDescriptorTable(2, srvHandle);
    commandList_->SetPipelineState(worldPipelineState_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = mesh.vertexGpuAddress;
    vbv.StrideInBytes = worldVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(mesh.vertexBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);

    D3D12_INDEX_BUFFER_VIEW ibv{};
    ibv.BufferLocation = mesh.indexGpuAddress;
    ibv.Format = DXGI_FORMAT_R32_UINT;
    ibv.SizeInBytes = static_cast<UINT>(mesh.indexBytes);
    commandList_->IASetIndexBuffer(&ibv);
    commandList_->DrawIndexedInstanced(static_cast<UINT>(mesh.indexCount), 1, 0, 0, 0);
#else
    (void)mesh;
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
    // Refuse truncated indexed draws instead of rendering corrupted geometry when
    // a mesh exceeds the shared world dynamic buffer capacity.
    if (safeVertexCount != vertexCount || safeIndexCount != indexCount) {
        return;
    }

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
