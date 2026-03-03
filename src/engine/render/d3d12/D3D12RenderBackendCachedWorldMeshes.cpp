#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"

#include <cstring>
#include <string>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#endif

#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
#endif

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
    static constexpr float kIdentity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    float vsConstants[32] = {};
    std::memcpy(vsConstants, viewProjectionMatrix4x4, sizeof(float) * 16u);
    std::memcpy(vsConstants + 16u, kIdentity, sizeof(float) * 16u);
    commandList_->SetGraphicsRoot32BitConstants(0, 32, vsConstants, 0);
    const WorldPsConstants worldPs = makeWorldPsConstants(nullptr, 0.0f);
    commandList_->SetGraphicsRoot32BitConstants(
        1,
        static_cast<UINT>(sizeof(WorldPsConstants) / sizeof(float)),
        &worldPs,
        0);
    D3D12_GPU_DESCRIPTOR_HANDLE srvBaseHandle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE srvNormalHandle = srvBaseHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE srvMetalRoughHandle = srvBaseHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE srvOcclusionHandle = srvBaseHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE srvEmissiveHandle = srvBaseHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE srvEnvHandle = srvBaseHandle;
    srvBaseHandle.ptr += static_cast<SIZE_T>(worldFallbackTextureDescriptorIndex_) *
                         static_cast<SIZE_T>(srvDescriptorSize_);
    srvNormalHandle.ptr += static_cast<SIZE_T>(worldFallbackNormalTextureDescriptorIndex_) *
                           static_cast<SIZE_T>(srvDescriptorSize_);
    srvMetalRoughHandle.ptr +=
        static_cast<SIZE_T>(worldFallbackMetallicRoughnessTextureDescriptorIndex_) *
        static_cast<SIZE_T>(srvDescriptorSize_);
    srvOcclusionHandle.ptr += static_cast<SIZE_T>(worldFallbackOcclusionTextureDescriptorIndex_) *
                              static_cast<SIZE_T>(srvDescriptorSize_);
    srvEmissiveHandle.ptr += static_cast<SIZE_T>(worldFallbackEmissiveTextureDescriptorIndex_) *
                             static_cast<SIZE_T>(srvDescriptorSize_);
    srvEnvHandle.ptr += static_cast<SIZE_T>(worldFallbackEnvTextureDescriptorIndex_) *
                        static_cast<SIZE_T>(srvDescriptorSize_);
    commandList_->SetGraphicsRootDescriptorTable(2, srvBaseHandle);
    commandList_->SetGraphicsRootDescriptorTable(3, srvNormalHandle);
    commandList_->SetGraphicsRootDescriptorTable(4, srvMetalRoughHandle);
    commandList_->SetGraphicsRootDescriptorTable(5, srvOcclusionHandle);
    commandList_->SetGraphicsRootDescriptorTable(6, srvEmissiveHandle);
    commandList_->SetGraphicsRootDescriptorTable(7, srvEnvHandle);
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
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(mesh.indexCount / 3u);
#else
    (void)mesh;
    (void)viewProjectionMatrix4x4;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}
