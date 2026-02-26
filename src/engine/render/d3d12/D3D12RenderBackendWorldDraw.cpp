#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"

#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#endif

#if defined(_WIN32)
using namespace engine::render::d3d12_internal;
#endif

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
