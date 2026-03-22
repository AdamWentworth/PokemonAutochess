#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/core/Environment.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
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

namespace {
void packWorldVsConstants(const float* viewProjectionMatrix4x4,
                          const float* modelMatrix4x4,
                          bool skinningEnabled,
                          std::uint32_t skinMatrixCount,
                          std::uint32_t skinningMode,
                          float* out36) {
    if (!out36) return;
    static constexpr float kIdentity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    const float* vp = viewProjectionMatrix4x4 ? viewProjectionMatrix4x4 : kIdentity;
    const float* model = modelMatrix4x4 ? modelMatrix4x4 : kIdentity;
    std::memcpy(out36, vp, sizeof(float) * 16u);
    std::memcpy(out36 + 16u, model, sizeof(float) * 16u);
    out36[32] = skinningEnabled ? 1.0f : 0.0f;
    out36[33] = static_cast<float>(skinMatrixCount);
    out36[34] = static_cast<float>(skinningMode);
    out36[35] = 0.0f;
}

bool pbrBindingLogEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_PBR_BIND_LOG");
        if (!env.has_value()) return false;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

int pbrDebugViewMode() {
    static const int mode = []() -> int {
        const auto env = engine::env::get("PAC_BACKEND_PBR_DEBUG_VIEW");
        if (!env.has_value()) return 0;
        try {
            return std::clamp(std::atoi(env->c_str()), 0, 8);
        } catch (...) {
            return 0;
        }
    }();
    return mode;
}

int pbrBindingLogMaxEntries() {
    static const int maxEntries = []() -> int {
        const auto env = engine::env::get("PAC_BACKEND_PBR_BIND_LOG_MAX");
        if (!env.has_value()) return 64;
        try {
            return (std::max)(1, std::atoi(env->c_str()));
        } catch (...) {
            return 64;
        }
    }();
    return maxEntries;
}

void maybeLogPbrBindingD3D12(const IRenderBackend::WorldTextureData* texture,
                             bool hasBase,
                             bool hasNormal,
                             bool hasMetallicRoughness,
                             bool hasOcclusion,
                             bool hasEmissive) {
    if (!pbrBindingLogEnabled()) return;
    if (!texture || texture->materialMode < 2u) return;
    static int sPrinted = 0;
    if (sPrinted >= pbrBindingLogMaxEntries()) return;
    ++sPrinted;
    std::cout
        << "[PBRBind][D3D12] key=" << (texture->key ? texture->key : "<null>")
        << " mode=" << static_cast<int>(texture->materialMode)
        << " has(base/norm/mr/occ/emi)="
        << (hasBase ? "1" : "0") << "/"
        << (hasNormal ? "1" : "0") << "/"
        << (hasMetallicRoughness ? "1" : "0") << "/"
        << (hasOcclusion ? "1" : "0") << "/"
        << (hasEmissive ? "1" : "0")
        << " texSize=" << texture->width << "x" << texture->height
        << " normSize=" << texture->normalWidth << "x" << texture->normalHeight
        << " mrSize=" << texture->metallicRoughnessWidth << "x" << texture->metallicRoughnessHeight
        << " roughF=" << texture->roughnessFactor
        << " metalF=" << texture->metallicFactor
        << " occF=" << texture->occlusionStrength
        << "\n";
}

void packWorldInstanceVertexData(const IRenderBackend::WorldMeshInstance& instance,
                                 WorldInstanceVertexData& out) {
    out.model0x = instance.modelMatrix[0];
    out.model0y = instance.modelMatrix[1];
    out.model0z = instance.modelMatrix[2];
    out.model0w = instance.modelMatrix[3];
    out.model1x = instance.modelMatrix[4];
    out.model1y = instance.modelMatrix[5];
    out.model1z = instance.modelMatrix[6];
    out.model1w = instance.modelMatrix[7];
    out.model2x = instance.modelMatrix[8];
    out.model2y = instance.modelMatrix[9];
    out.model2z = instance.modelMatrix[10];
    out.model2w = instance.modelMatrix[11];
    out.model3x = instance.modelMatrix[12];
    out.model3y = instance.modelMatrix[13];
    out.model3z = instance.modelMatrix[14];
    out.model3w = instance.modelMatrix[15];
    out.colorR = instance.vertexColorMulR;
    out.colorG = instance.vertexColorMulG;
    out.colorB = instance.vertexColorMulB;
    out.colorA = instance.vertexColorMulA;
}
} // namespace

void D3D12RenderBackend::drawWorldTriangles(const WorldTriangle* triangles,
                                            std::size_t triangleCount,
                                            const float* viewProjectionMatrix4x4,
                                            int surfaceWidth,
                                            int surfaceHeight) {
#if defined(_WIN32)
    if (!recording_ || !triangles || triangleCount == 0 || !viewProjectionMatrix4x4) return;
    if (surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!worldPipelineState_ || !worldRootSignature_ || !worldVertexBuffer_ || !commandList_ || !srvHeap_) return;
    if (!worldVertexMappedData_ || !worldVsConstantMappedData_ || !worldSkinMatrixMappedData_) return;
    ensureWorldFallbackEnvTexture();

    const std::size_t safeCount = (triangleCount > kMaxWorldTriangles) ? kMaxWorldTriangles : triangleCount;
    if (safeCount == 0) return;
    const std::size_t vertexCount = safeCount * 3;
    const std::size_t neededBytes = vertexCount * sizeof(WorldVertex);
    const std::size_t writeOffset = alignUp(static_cast<std::size_t>(worldVertexFrameOffset_), 256u);
    const std::size_t vsConstantsWriteOffset =
        alignUp(static_cast<std::size_t>(worldVsConstantFrameOffset_), 256u);
    if (neededBytes == 0 || writeOffset + neededBytes > worldVertexBufferSize_) return;
    if (vsConstantsWriteOffset + 256u > worldVsConstantBufferSize_) return;

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
    float vsConstants[36] = {};
    packWorldVsConstants(viewProjectionMatrix4x4, nullptr, false, 0u, 0u, vsConstants);
    std::memcpy(
        worldVsConstantMappedData_ + vsConstantsWriteOffset,
        vsConstants,
        sizeof(vsConstants));
    commandList_->SetGraphicsRootConstantBufferView(
        0,
        worldVsConstantBufferGpuAddress_ + static_cast<std::uint64_t>(vsConstantsWriteOffset));
    commandList_->SetGraphicsRootConstantBufferView(2, worldSkinMatrixBufferGpuAddress_);
    commandList_->SetGraphicsRootShaderResourceView(9, worldInstanceBufferGpuAddress_);
    const WorldPsConstants worldPs = makeWorldPsConstants(nullptr, 0.0f);
    commandList_->SetGraphicsRoot32BitConstants(
        1,
        static_cast<UINT>(sizeof(WorldPsConstants) / sizeof(float)),
        &worldPs,
        0);
    if (srvHeap_) {
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
        commandList_->SetGraphicsRootDescriptorTable(3, srvBaseHandle);
        commandList_->SetGraphicsRootDescriptorTable(4, srvNormalHandle);
        commandList_->SetGraphicsRootDescriptorTable(5, srvMetalRoughHandle);
        commandList_->SetGraphicsRootDescriptorTable(6, srvOcclusionHandle);
        commandList_->SetGraphicsRootDescriptorTable(7, srvEmissiveHandle);
        commandList_->SetGraphicsRootDescriptorTable(8, srvEnvHandle);
    }
    commandList_->SetPipelineState(worldPipelineState_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = worldVertexBufferGpuAddress_ + static_cast<std::uint64_t>(writeOffset);
    vbv.StrideInBytes = worldVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(neededBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->DrawInstanced(static_cast<UINT>(vertexCount), 1, 0, 0);
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(vertexCount / 3u);
    worldVertexFrameOffset_ = static_cast<UINT>(writeOffset + neededBytes);
    worldVsConstantFrameOffset_ = static_cast<UINT>(vsConstantsWriteOffset + 256u);
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
    ensureWorldFallbackEnvTexture();
    drawWorldIndexedMeshInternal(
        vertices,
        vertexCount,
        indices,
        indexCount,
        worldFallbackTextureDescriptorIndex_,
        worldFallbackNormalTextureDescriptorIndex_,
        worldFallbackMetallicRoughnessTextureDescriptorIndex_,
        worldFallbackOcclusionTextureDescriptorIndex_,
        worldFallbackEmissiveTextureDescriptorIndex_,
        worldFallbackEnvTextureDescriptorIndex_,
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
    ensureWorldFallbackEnvTexture();
    SpriteTexture* worldTex = ensureWorldTexture(texture);
    const std::uint32_t baseDescriptorIndex =
        worldTex ? worldTex->descriptorIndex : worldFallbackTextureDescriptorIndex_;
    const float useTexture = (worldTex && worldTex->valid) ? 1.0f : 0.0f;

    SpriteTexture* normalTex = texture ? ensureWorldTextureRaw(
        texture->normalKey,
        texture->normalCacheKey,
        texture->normalRgba,
        texture->normalWidth,
        texture->normalHeight,
        texture->normalWrapS,
        texture->normalWrapT,
        /*srgb=*/false) : nullptr;
    const std::uint32_t normalDescriptorIndex =
        normalTex ? normalTex->descriptorIndex : worldFallbackNormalTextureDescriptorIndex_;

    SpriteTexture* metallicRoughnessTex = texture ? ensureWorldTextureRaw(
        texture->metallicRoughnessKey,
        texture->metallicRoughnessCacheKey,
        texture->metallicRoughnessRgba,
        texture->metallicRoughnessWidth,
        texture->metallicRoughnessHeight,
        texture->metallicRoughnessWrapS,
        texture->metallicRoughnessWrapT,
        /*srgb=*/false) : nullptr;
    const std::uint32_t metallicRoughnessDescriptorIndex =
        metallicRoughnessTex
            ? metallicRoughnessTex->descriptorIndex
            : worldFallbackMetallicRoughnessTextureDescriptorIndex_;

    SpriteTexture* occlusionTex = texture ? ensureWorldTextureRaw(
        texture->occlusionKey,
        texture->occlusionCacheKey,
        texture->occlusionRgba,
        texture->occlusionWidth,
        texture->occlusionHeight,
        texture->occlusionWrapS,
        texture->occlusionWrapT,
        /*srgb=*/false) : nullptr;
    const std::uint32_t occlusionDescriptorIndex =
        occlusionTex ? occlusionTex->descriptorIndex : worldFallbackOcclusionTextureDescriptorIndex_;

    SpriteTexture* emissiveTex = texture ? ensureWorldTextureRaw(
        texture->emissiveKey,
        texture->emissiveCacheKey,
        texture->emissiveRgba,
        texture->emissiveWidth,
        texture->emissiveHeight,
        texture->emissiveWrapS,
        texture->emissiveWrapT,
        /*srgb=*/true) : nullptr;
    const std::uint32_t emissiveDescriptorIndex =
        emissiveTex ? emissiveTex->descriptorIndex : worldFallbackEmissiveTextureDescriptorIndex_;

    maybeLogPbrBindingD3D12(
        texture,
        worldTex != nullptr,
        normalTex != nullptr,
        metallicRoughnessTex != nullptr,
        occlusionTex != nullptr,
        emissiveTex != nullptr);

    drawWorldIndexedMeshInternal(
        vertices,
        vertexCount,
        indices,
        indexCount,
        baseDescriptorIndex,
        normalDescriptorIndex,
        metallicRoughnessDescriptorIndex,
        occlusionDescriptorIndex,
        emissiveDescriptorIndex,
        worldFallbackEnvTextureDescriptorIndex_,
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

void D3D12RenderBackend::drawWorldIndexedMeshTexturedCached(const char* geometryKey,
                                                            const WorldMeshVertex* vertices,
                                                            std::size_t vertexCount,
                                                            const std::uint32_t* indices,
                                                            std::size_t indexCount,
                                                            const WorldTextureData* texture,
                                                            const float* viewProjectionMatrix4x4,
                                                            int surfaceWidth,
                                                            int surfaceHeight) {
#if defined(_WIN32)
    if (!geometryKey || geometryKey[0] == '\0') {
        drawWorldIndexedMeshTextured(
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }

    CachedWorldMesh* cached =
        ensureCachedWorldMesh(geometryKey, vertices, vertexCount, indices, indexCount);
    if (!cached || !cached->valid) {
        drawWorldIndexedMeshTextured(
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }

    ensureWorldFallbackEnvTexture();
    SpriteTexture* worldTex = ensureWorldTexture(texture);
    const std::uint32_t baseDescriptorIndex =
        worldTex ? worldTex->descriptorIndex : worldFallbackTextureDescriptorIndex_;
    const float useTexture = (worldTex && worldTex->valid) ? 1.0f : 0.0f;

    SpriteTexture* normalTex = texture ? ensureWorldTextureRaw(
        texture->normalKey,
        texture->normalCacheKey,
        texture->normalRgba,
        texture->normalWidth,
        texture->normalHeight,
        texture->normalWrapS,
        texture->normalWrapT,
        /*srgb=*/false) : nullptr;
    const std::uint32_t normalDescriptorIndex =
        normalTex ? normalTex->descriptorIndex : worldFallbackNormalTextureDescriptorIndex_;

    SpriteTexture* metallicRoughnessTex = texture ? ensureWorldTextureRaw(
        texture->metallicRoughnessKey,
        texture->metallicRoughnessCacheKey,
        texture->metallicRoughnessRgba,
        texture->metallicRoughnessWidth,
        texture->metallicRoughnessHeight,
        texture->metallicRoughnessWrapS,
        texture->metallicRoughnessWrapT,
        /*srgb=*/false) : nullptr;
    const std::uint32_t metallicRoughnessDescriptorIndex =
        metallicRoughnessTex
            ? metallicRoughnessTex->descriptorIndex
            : worldFallbackMetallicRoughnessTextureDescriptorIndex_;

    SpriteTexture* occlusionTex = texture ? ensureWorldTextureRaw(
        texture->occlusionKey,
        texture->occlusionCacheKey,
        texture->occlusionRgba,
        texture->occlusionWidth,
        texture->occlusionHeight,
        texture->occlusionWrapS,
        texture->occlusionWrapT,
        /*srgb=*/false) : nullptr;
    const std::uint32_t occlusionDescriptorIndex =
        occlusionTex ? occlusionTex->descriptorIndex : worldFallbackOcclusionTextureDescriptorIndex_;

    SpriteTexture* emissiveTex = texture ? ensureWorldTextureRaw(
        texture->emissiveKey,
        texture->emissiveCacheKey,
        texture->emissiveRgba,
        texture->emissiveWidth,
        texture->emissiveHeight,
        texture->emissiveWrapS,
        texture->emissiveWrapT,
        /*srgb=*/true) : nullptr;
    const std::uint32_t emissiveDescriptorIndex =
        emissiveTex ? emissiveTex->descriptorIndex : worldFallbackEmissiveTextureDescriptorIndex_;

    maybeLogPbrBindingD3D12(
        texture,
        worldTex != nullptr,
        normalTex != nullptr,
        metallicRoughnessTex != nullptr,
        occlusionTex != nullptr,
        emissiveTex != nullptr);

    drawWorldIndexedMeshTexturedCachedInternal(
        *cached,
        vertices,
        vertexCount,
        indices,
        indexCount,
        baseDescriptorIndex,
        normalDescriptorIndex,
        metallicRoughnessDescriptorIndex,
        occlusionDescriptorIndex,
        emissiveDescriptorIndex,
        worldFallbackEnvTextureDescriptorIndex_,
        texture,
        useTexture,
        viewProjectionMatrix4x4,
        worldInstanceBufferGpuAddress_,
        1u,
        surfaceWidth,
        surfaceHeight);
#else
    (void)geometryKey;
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

void D3D12RenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
    const char* geometryKey,
    const WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount,
    const WorldTextureData* texture,
    const WorldMeshInstance* instances,
    std::size_t instanceCount,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight) {
#if defined(_WIN32)
    if (!instances || instanceCount == 0u) {
        drawWorldIndexedMeshTexturedCached(
            geometryKey,
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }
    if (!geometryKey || geometryKey[0] == '\0' || !worldInstanceBuffer_ || !worldInstanceMappedData_) {
        IRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
            geometryKey,
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            instances,
            instanceCount,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }

    CachedWorldMesh* cached =
        ensureCachedWorldMesh(geometryKey, vertices, vertexCount, indices, indexCount);
    if (!cached || !cached->valid) {
        IRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
            geometryKey,
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            instances,
            instanceCount,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }

    if (instanceCount > static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())) {
        IRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
            geometryKey,
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            instances,
            instanceCount,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }

    const std::size_t instanceBytes = instanceCount * sizeof(WorldInstanceVertexData);
    const std::size_t instanceWriteOffset = static_cast<std::size_t>(worldInstanceFrameOffset_);
    if (instanceWriteOffset + instanceBytes > worldInstanceBufferSize_) {
        IRenderBackend::drawWorldIndexedMeshTexturedCachedInstanced(
            geometryKey,
            vertices,
            vertexCount,
            indices,
            indexCount,
            texture,
            instances,
            instanceCount,
            viewProjectionMatrix4x4,
            surfaceWidth,
            surfaceHeight);
        return;
    }

    auto* instanceData =
        reinterpret_cast<WorldInstanceVertexData*>(worldInstanceMappedData_ + instanceWriteOffset);
    for (std::size_t i = 0; i < instanceCount; ++i) {
        packWorldInstanceVertexData(instances[i], instanceData[i]);
    }
    const D3D12_GPU_VIRTUAL_ADDRESS instanceDataGpuAddress =
        worldInstanceBufferGpuAddress_ + static_cast<std::uint64_t>(instanceWriteOffset);
    worldInstanceFrameOffset_ =
        static_cast<std::uint32_t>(instanceWriteOffset + instanceBytes);

    ensureWorldFallbackEnvTexture();
    SpriteTexture* worldTex = ensureWorldTexture(texture);
    const std::uint32_t baseDescriptorIndex =
        worldTex ? worldTex->descriptorIndex : worldFallbackTextureDescriptorIndex_;
    const float useTexture = (worldTex && worldTex->valid) ? 1.0f : 0.0f;

    SpriteTexture* normalTex = texture ? ensureWorldTextureRaw(
        texture->normalKey,
        texture->normalCacheKey,
        texture->normalRgba,
        texture->normalWidth,
        texture->normalHeight,
        texture->normalWrapS,
        texture->normalWrapT,
        /*srgb=*/false) : nullptr;
    const std::uint32_t normalDescriptorIndex =
        normalTex ? normalTex->descriptorIndex : worldFallbackNormalTextureDescriptorIndex_;

    SpriteTexture* metallicRoughnessTex = texture ? ensureWorldTextureRaw(
        texture->metallicRoughnessKey,
        texture->metallicRoughnessCacheKey,
        texture->metallicRoughnessRgba,
        texture->metallicRoughnessWidth,
        texture->metallicRoughnessHeight,
        texture->metallicRoughnessWrapS,
        texture->metallicRoughnessWrapT,
        /*srgb=*/false) : nullptr;
    const std::uint32_t metallicRoughnessDescriptorIndex =
        metallicRoughnessTex
            ? metallicRoughnessTex->descriptorIndex
            : worldFallbackMetallicRoughnessTextureDescriptorIndex_;

    SpriteTexture* occlusionTex = texture ? ensureWorldTextureRaw(
        texture->occlusionKey,
        texture->occlusionCacheKey,
        texture->occlusionRgba,
        texture->occlusionWidth,
        texture->occlusionHeight,
        texture->occlusionWrapS,
        texture->occlusionWrapT,
        /*srgb=*/false) : nullptr;
    const std::uint32_t occlusionDescriptorIndex =
        occlusionTex ? occlusionTex->descriptorIndex : worldFallbackOcclusionTextureDescriptorIndex_;

    SpriteTexture* emissiveTex = texture ? ensureWorldTextureRaw(
        texture->emissiveKey,
        texture->emissiveCacheKey,
        texture->emissiveRgba,
        texture->emissiveWidth,
        texture->emissiveHeight,
        texture->emissiveWrapS,
        texture->emissiveWrapT,
        /*srgb=*/true) : nullptr;
    const std::uint32_t emissiveDescriptorIndex =
        emissiveTex ? emissiveTex->descriptorIndex : worldFallbackEmissiveTextureDescriptorIndex_;

    maybeLogPbrBindingD3D12(
        texture,
        worldTex != nullptr,
        normalTex != nullptr,
        metallicRoughnessTex != nullptr,
        occlusionTex != nullptr,
        emissiveTex != nullptr);

    drawWorldIndexedMeshTexturedCachedInternal(
        *cached,
        vertices,
        vertexCount,
        indices,
        indexCount,
        baseDescriptorIndex,
        normalDescriptorIndex,
        metallicRoughnessDescriptorIndex,
        occlusionDescriptorIndex,
        emissiveDescriptorIndex,
        worldFallbackEnvTextureDescriptorIndex_,
        texture,
        useTexture,
        viewProjectionMatrix4x4,
        instanceDataGpuAddress,
        static_cast<std::uint32_t>(instanceCount),
        surfaceWidth,
        surfaceHeight);
#else
    (void)geometryKey;
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    (void)texture;
    (void)instances;
    (void)instanceCount;
    (void)viewProjectionMatrix4x4;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::drawWorldIndexedMeshInternal(const WorldMeshVertex* vertices,
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
        !worldVsConstantBuffer_ ||
        !worldSkinMatrixBuffer_ ||
        !commandList_ ||
        !srvHeap_) {
        return;
    }
    if (!worldVertexMappedData_ ||
        !worldIndexMappedData_ ||
        !worldVsConstantMappedData_ ||
        !worldSkinMatrixMappedData_) {
        return;
    }

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
    const std::size_t vsConstantsWriteOffset =
        alignUp(static_cast<std::size_t>(worldVsConstantFrameOffset_), 256u);
    if (vertexWriteOffset + vertexBytes > worldVertexBufferSize_ ||
        indexWriteOffset + indexBytes > worldIndexBufferSize_ ||
        vsConstantsWriteOffset + 256u > worldVsConstantBufferSize_) {
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
    const float* modelMatrix = textureData ? textureData->modelMatrix.data() : nullptr;
    constexpr std::uint32_t kMaxGpuSkinMatrices = 64u;
    bool gpuSkinningEnabled =
        textureData &&
        textureData->gpuSkinning != 0u &&
        textureData->skinMatrices != nullptr &&
        textureData->skinMatrixCount > 0u &&
        textureData->skinMatrixCount <= kMaxGpuSkinMatrices;
    const std::uint32_t gpuSkinningMode =
        gpuSkinningEnabled ? std::min<std::uint32_t>(textureData->gpuSkinningMode, 1u) : 0u;
    std::uint32_t gpuSkinMatrixCount = gpuSkinningEnabled ? textureData->skinMatrixCount : 0u;
    D3D12_GPU_VIRTUAL_ADDRESS skinMatrixGpuAddress = worldSkinMatrixBufferGpuAddress_;
    if (gpuSkinningEnabled) {
        if (textureData->skinMatrices == lastWorldSkinMatrices_ &&
            gpuSkinningMode == lastWorldSkinningMode_ &&
            gpuSkinMatrixCount == lastWorldSkinMatrixCount_) {
            skinMatrixGpuAddress = lastWorldSkinMatrixGpuAddress_;
        } else {
            const std::size_t copyBytes =
                static_cast<std::size_t>(gpuSkinMatrixCount) *
                (gpuSkinningMode == 1u ? 32u : 16u) * sizeof(float);
            const std::size_t skinWriteOffset =
                alignUp(static_cast<std::size_t>(worldSkinMatrixFrameOffset_), 256u);
            const std::size_t skinWriteEnd = skinWriteOffset + alignUp(copyBytes, 256u);
            if (skinWriteEnd <= worldSkinMatrixBufferSize_) {
                std::memcpy(
                    worldSkinMatrixMappedData_ + skinWriteOffset,
                    textureData->skinMatrices,
                    copyBytes);
                skinMatrixGpuAddress += static_cast<std::uint64_t>(skinWriteOffset);
                worldSkinMatrixFrameOffset_ = static_cast<UINT>(skinWriteEnd);
                lastWorldSkinMatrices_ = textureData->skinMatrices;
                lastWorldSkinningMode_ = static_cast<std::uint8_t>(gpuSkinningMode);
                lastWorldSkinMatrixCount_ = gpuSkinMatrixCount;
                lastWorldSkinMatrixGpuAddress_ = skinMatrixGpuAddress;
            } else {
                gpuSkinningEnabled = false;
                gpuSkinMatrixCount = 0u;
            }
        }
    }

    float vsConstants[36] = {};
    packWorldVsConstants(
        viewProjectionMatrix4x4,
        modelMatrix,
        gpuSkinningEnabled,
        gpuSkinMatrixCount,
        gpuSkinningMode,
        vsConstants);
    std::memcpy(
        worldVsConstantMappedData_ + vsConstantsWriteOffset,
        vsConstants,
        sizeof(vsConstants));
    commandList_->SetGraphicsRootConstantBufferView(
        0,
        worldVsConstantBufferGpuAddress_ + static_cast<std::uint64_t>(vsConstantsWriteOffset));
    commandList_->SetGraphicsRootConstantBufferView(2, skinMatrixGpuAddress);
    commandList_->SetGraphicsRootShaderResourceView(9, worldInstanceBufferGpuAddress_);
    WorldPsConstants worldPs = makeWorldPsConstants(textureData, useTexture);
    if (textureData && textureData->materialMode >= 2u) {
        // Reuse an unused packed slot in lit model mode for shader debug-view selection.
        worldPs.materialFlipbook1Fps = static_cast<float>(pbrDebugViewMode());
    }
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
    srvBaseHandle.ptr += static_cast<SIZE_T>(baseTextureDescriptorIndex) * static_cast<SIZE_T>(srvDescriptorSize_);
    srvNormalHandle.ptr +=
        static_cast<SIZE_T>(normalTextureDescriptorIndex) * static_cast<SIZE_T>(srvDescriptorSize_);
    srvMetalRoughHandle.ptr += static_cast<SIZE_T>(metallicRoughnessTextureDescriptorIndex) *
                               static_cast<SIZE_T>(srvDescriptorSize_);
    srvOcclusionHandle.ptr +=
        static_cast<SIZE_T>(occlusionTextureDescriptorIndex) * static_cast<SIZE_T>(srvDescriptorSize_);
    srvEmissiveHandle.ptr +=
        static_cast<SIZE_T>(emissiveTextureDescriptorIndex) * static_cast<SIZE_T>(srvDescriptorSize_);
    srvEnvHandle.ptr +=
        static_cast<SIZE_T>(envTextureDescriptorIndex) * static_cast<SIZE_T>(srvDescriptorSize_);
    commandList_->SetGraphicsRootDescriptorTable(3, srvBaseHandle);
    commandList_->SetGraphicsRootDescriptorTable(4, srvNormalHandle);
    commandList_->SetGraphicsRootDescriptorTable(5, srvMetalRoughHandle);
    commandList_->SetGraphicsRootDescriptorTable(6, srvOcclusionHandle);
    commandList_->SetGraphicsRootDescriptorTable(7, srvEmissiveHandle);
    commandList_->SetGraphicsRootDescriptorTable(8, srvEnvHandle);
    frameIndexedD3d12DescriptorTableSets_ += 6u;
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
    ++frameIndexedD3d12PsoSets_;
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
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(safeIndexCount / 3u);

    std::size_t nextVertexOffset = vertexWriteOffset + vertexBytes;
    std::size_t nextIndexOffset = indexWriteOffset + indexBytes;
    std::size_t nextVsConstantOffset = vsConstantsWriteOffset + 256u;

    const bool drawCharacterOutline =
        textureData &&
        textureData->characterInkingEnabled != 0u &&
        textureData->materialMode >= 2u &&
        safeVertexCount > 0u;
    if (drawCharacterOutline) {
        const std::size_t outlineVertexWriteOffset = alignUp(nextVertexOffset, 256u);
        const std::size_t outlineIndexWriteOffset = alignUp(nextIndexOffset, 256u);
        const std::size_t outlineVsConstantWriteOffset = alignUp(nextVsConstantOffset, 256u);
        if (outlineVertexWriteOffset + vertexBytes <= worldVertexBufferSize_ &&
            outlineIndexWriteOffset + indexBytes <= worldIndexBufferSize_ &&
            outlineVsConstantWriteOffset + 256u <= worldVsConstantBufferSize_) {
            auto* outlineVertices =
                reinterpret_cast<WorldVertex*>(worldVertexMappedData_ + outlineVertexWriteOffset);
            const auto* srcVertices = reinterpret_cast<const WorldVertex*>(vertices);
            std::memcpy(outlineVertices, srcVertices, vertexBytes);

            const float kOutlineExtrude = 0.001f;
            for (std::size_t vi = 0; vi < safeVertexCount; ++vi) {
                WorldVertex& v = outlineVertices[vi];
                const float lenSq = v.nx * v.nx + v.ny * v.ny + v.nz * v.nz;
                if (lenSq > 1e-10f) {
                    const float invLen = 1.0f / std::sqrt(lenSq);
                    v.x += v.nx * invLen * kOutlineExtrude;
                    v.y += v.ny * invLen * kOutlineExtrude;
                    v.z += v.nz * invLen * kOutlineExtrude;
                }
                v.r = 0.0f;
                v.g = 0.0f;
                v.b = 0.0f;
                v.a = 1.0f;
            }

            auto* outlineIndices =
                reinterpret_cast<std::uint32_t*>(worldIndexMappedData_ + outlineIndexWriteOffset);
            std::memcpy(outlineIndices, indices, indexBytes);

            D3D12_VERTEX_BUFFER_VIEW outlineVbv{};
            outlineVbv.BufferLocation =
                worldVertexBufferGpuAddress_ + static_cast<std::uint64_t>(outlineVertexWriteOffset);
            outlineVbv.StrideInBytes = worldVertexStride_;
            outlineVbv.SizeInBytes = static_cast<UINT>(vertexBytes);
            commandList_->IASetVertexBuffers(0, 1, &outlineVbv);

            D3D12_INDEX_BUFFER_VIEW outlineIbv{};
            outlineIbv.BufferLocation =
                worldIndexBufferGpuAddress_ + static_cast<std::uint64_t>(outlineIndexWriteOffset);
            outlineIbv.Format = DXGI_FORMAT_R32_UINT;
            outlineIbv.SizeInBytes = static_cast<UINT>(indexBytes);
            commandList_->IASetIndexBuffer(&outlineIbv);

            float outlineVsConstants[36] = {};
    packWorldVsConstants(
        viewProjectionMatrix4x4,
        modelMatrix,
        false,
        0u,
        0u,
        outlineVsConstants);
            std::memcpy(
                worldVsConstantMappedData_ + outlineVsConstantWriteOffset,
                outlineVsConstants,
                sizeof(outlineVsConstants));
            commandList_->SetGraphicsRootConstantBufferView(
                0,
                worldVsConstantBufferGpuAddress_ +
                    static_cast<std::uint64_t>(outlineVsConstantWriteOffset));
            commandList_->SetGraphicsRootConstantBufferView(2, worldSkinMatrixBufferGpuAddress_);

            WorldPsConstants outlinePs = makeWorldPsConstants(textureData, 0.0f);
            outlinePs.materialMode = 3.0f;
            commandList_->SetGraphicsRoot32BitConstants(
                1,
                static_cast<UINT>(sizeof(WorldPsConstants) / sizeof(float)),
                &outlinePs,
                0);
            commandList_->SetPipelineState(worldPipelineState_.Get());
            ++frameIndexedD3d12PsoSets_;
            commandList_->DrawIndexedInstanced(static_cast<UINT>(safeIndexCount), 1, 0, 0, 0);
            ++frameDrawCalls_;
            frameTriangles_ += static_cast<std::uint64_t>(safeIndexCount / 3u);

            nextVertexOffset = outlineVertexWriteOffset + vertexBytes;
            nextIndexOffset = outlineIndexWriteOffset + indexBytes;
            nextVsConstantOffset = outlineVsConstantWriteOffset + 256u;
        }
    }

    worldVertexFrameOffset_ = static_cast<UINT>(nextVertexOffset);
    worldIndexFrameOffset_ = static_cast<UINT>(nextIndexOffset);
    worldVsConstantFrameOffset_ = static_cast<UINT>(nextVsConstantOffset);
#else
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    (void)baseTextureDescriptorIndex;
    (void)normalTextureDescriptorIndex;
    (void)metallicRoughnessTextureDescriptorIndex;
    (void)occlusionTextureDescriptorIndex;
    (void)emissiveTextureDescriptorIndex;
    (void)envTextureDescriptorIndex;
    (void)textureData;
    (void)useTexture;
    (void)viewProjectionMatrix4x4;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}

void D3D12RenderBackend::drawWorldIndexedMeshTexturedCachedInternal(
    const CachedWorldMesh& mesh,
    const WorldMeshVertex* vertices,
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
    D3D12_GPU_VIRTUAL_ADDRESS instanceDataGpuAddress,
    std::uint32_t instanceCount,
    int surfaceWidth,
    int surfaceHeight) {
#if defined(_WIN32)
    if (!recording_ || !viewProjectionMatrix4x4) return;
    if (!mesh.valid || !mesh.vertexBuffer || !mesh.indexBuffer || mesh.indexCount < 3u) return;
    if (instanceCount == 0u || instanceDataGpuAddress == 0u) return;
    if (surfaceWidth <= 0 || surfaceHeight <= 0) return;
    if (!worldPipelineState_ ||
        !worldRootSignature_ ||
        !worldVsConstantBuffer_ ||
        !worldSkinMatrixBuffer_ ||
        !commandList_ ||
        !srvHeap_) {
        return;
    }
    if (!worldVsConstantMappedData_ || !worldSkinMatrixMappedData_) return;

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

    const float* modelMatrix = textureData ? textureData->modelMatrix.data() : nullptr;
    constexpr std::uint32_t kMaxGpuSkinMatrices = 64u;
    bool gpuSkinningEnabled =
        textureData &&
        textureData->gpuSkinning != 0u &&
        textureData->skinMatrices != nullptr &&
        textureData->skinMatrixCount > 0u &&
        textureData->skinMatrixCount <= kMaxGpuSkinMatrices;
    const std::uint32_t gpuSkinningMode =
        gpuSkinningEnabled ? std::min<std::uint32_t>(textureData->gpuSkinningMode, 1u) : 0u;
    std::uint32_t gpuSkinMatrixCount = gpuSkinningEnabled ? textureData->skinMatrixCount : 0u;
    D3D12_GPU_VIRTUAL_ADDRESS skinMatrixGpuAddress = worldSkinMatrixBufferGpuAddress_;
    if (gpuSkinningEnabled) {
        if (textureData->skinMatrices == lastWorldSkinMatrices_ &&
            gpuSkinningMode == lastWorldSkinningMode_ &&
            gpuSkinMatrixCount == lastWorldSkinMatrixCount_) {
            skinMatrixGpuAddress = lastWorldSkinMatrixGpuAddress_;
        } else {
            const std::size_t copyBytes =
                static_cast<std::size_t>(gpuSkinMatrixCount) *
                (gpuSkinningMode == 1u ? 32u : 16u) * sizeof(float);
            const std::size_t skinWriteOffset =
                alignUp(static_cast<std::size_t>(worldSkinMatrixFrameOffset_), 256u);
            const std::size_t skinWriteEnd = skinWriteOffset + alignUp(copyBytes, 256u);
            if (skinWriteEnd <= worldSkinMatrixBufferSize_) {
                std::memcpy(
                    worldSkinMatrixMappedData_ + skinWriteOffset,
                    textureData->skinMatrices,
                    copyBytes);
                skinMatrixGpuAddress += static_cast<std::uint64_t>(skinWriteOffset);
                worldSkinMatrixFrameOffset_ = static_cast<UINT>(skinWriteEnd);
                lastWorldSkinMatrices_ = textureData->skinMatrices;
                lastWorldSkinningMode_ = static_cast<std::uint8_t>(gpuSkinningMode);
                lastWorldSkinMatrixCount_ = gpuSkinMatrixCount;
                lastWorldSkinMatrixGpuAddress_ = skinMatrixGpuAddress;
            } else {
                gpuSkinningEnabled = false;
                gpuSkinMatrixCount = 0u;
            }
        }
    }

    const std::size_t vsConstantsWriteOffset =
        alignUp(static_cast<std::size_t>(worldVsConstantFrameOffset_), 256u);
    if (vsConstantsWriteOffset + 256u > worldVsConstantBufferSize_) return;

    float vsConstants[36] = {};
    packWorldVsConstants(
        viewProjectionMatrix4x4,
        modelMatrix,
        gpuSkinningEnabled,
        gpuSkinMatrixCount,
        gpuSkinningMode,
        vsConstants);
    std::memcpy(
        worldVsConstantMappedData_ + vsConstantsWriteOffset,
        vsConstants,
        sizeof(vsConstants));
    commandList_->SetGraphicsRootConstantBufferView(
        0,
        worldVsConstantBufferGpuAddress_ + static_cast<std::uint64_t>(vsConstantsWriteOffset));
    commandList_->SetGraphicsRootConstantBufferView(2, skinMatrixGpuAddress);
    commandList_->SetGraphicsRootShaderResourceView(9, instanceDataGpuAddress);
    WorldPsConstants worldPs = makeWorldPsConstants(textureData, useTexture);
    if (textureData && textureData->materialMode >= 2u) {
        worldPs.materialFlipbook1Fps = static_cast<float>(pbrDebugViewMode());
    }
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
    srvBaseHandle.ptr += static_cast<SIZE_T>(baseTextureDescriptorIndex) * static_cast<SIZE_T>(srvDescriptorSize_);
    srvNormalHandle.ptr +=
        static_cast<SIZE_T>(normalTextureDescriptorIndex) * static_cast<SIZE_T>(srvDescriptorSize_);
    srvMetalRoughHandle.ptr += static_cast<SIZE_T>(metallicRoughnessTextureDescriptorIndex) *
                               static_cast<SIZE_T>(srvDescriptorSize_);
    srvOcclusionHandle.ptr +=
        static_cast<SIZE_T>(occlusionTextureDescriptorIndex) * static_cast<SIZE_T>(srvDescriptorSize_);
    srvEmissiveHandle.ptr +=
        static_cast<SIZE_T>(emissiveTextureDescriptorIndex) * static_cast<SIZE_T>(srvDescriptorSize_);
    srvEnvHandle.ptr +=
        static_cast<SIZE_T>(envTextureDescriptorIndex) * static_cast<SIZE_T>(srvDescriptorSize_);
    commandList_->SetGraphicsRootDescriptorTable(3, srvBaseHandle);
    commandList_->SetGraphicsRootDescriptorTable(4, srvNormalHandle);
    commandList_->SetGraphicsRootDescriptorTable(5, srvMetalRoughHandle);
    commandList_->SetGraphicsRootDescriptorTable(6, srvOcclusionHandle);
    commandList_->SetGraphicsRootDescriptorTable(7, srvEmissiveHandle);
    commandList_->SetGraphicsRootDescriptorTable(8, srvEnvHandle);
    frameIndexedD3d12DescriptorTableSets_ += 6u;

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
    ++frameIndexedD3d12PsoSets_;
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

    commandList_->DrawIndexedInstanced(static_cast<UINT>(mesh.indexCount), instanceCount, 0, 0, 0);
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(mesh.indexCount / 3u) *
                      static_cast<std::uint64_t>(instanceCount);

    worldVsConstantFrameOffset_ = static_cast<UINT>(vsConstantsWriteOffset + 256u);

    const bool drawCharacterOutline =
        textureData &&
        textureData->characterInkingEnabled != 0u &&
        textureData->materialMode >= 2u &&
        vertices != nullptr &&
        indices != nullptr &&
        vertexCount > 0u &&
        indexCount >= 3u &&
        worldVertexBuffer_ &&
        worldIndexBuffer_ &&
        worldVertexMappedData_ &&
        worldIndexMappedData_;
    if (!drawCharacterOutline) {
        return;
    }

    const std::size_t maxVertexCapacity = worldVertexBufferSize_ / sizeof(WorldVertex);
    const std::size_t maxIndexCapacity = worldIndexBufferSize_ / sizeof(std::uint32_t);
    const std::size_t safeVertexCount = (std::min)(vertexCount, maxVertexCapacity);
    const std::size_t safeIndexCount = (std::min)(indexCount, maxIndexCapacity);
    if (safeVertexCount != vertexCount || safeIndexCount != indexCount || safeIndexCount < 3u) {
        return;
    }

    const std::size_t vertexBytes = safeVertexCount * sizeof(WorldVertex);
    const std::size_t indexBytes = safeIndexCount * sizeof(std::uint32_t);
    const std::size_t outlineVertexWriteOffset =
        alignUp(static_cast<std::size_t>(worldVertexFrameOffset_), 256u);
    const std::size_t outlineIndexWriteOffset =
        alignUp(static_cast<std::size_t>(worldIndexFrameOffset_), 256u);
    const std::size_t outlineVsConstantWriteOffset =
        alignUp(static_cast<std::size_t>(worldVsConstantFrameOffset_), 256u);
    if (outlineVertexWriteOffset + vertexBytes > worldVertexBufferSize_ ||
        outlineIndexWriteOffset + indexBytes > worldIndexBufferSize_ ||
        outlineVsConstantWriteOffset + 256u > worldVsConstantBufferSize_) {
        return;
    }

    auto* outlineVertices =
        reinterpret_cast<WorldVertex*>(worldVertexMappedData_ + outlineVertexWriteOffset);
    const auto* srcVertices = reinterpret_cast<const WorldVertex*>(vertices);
    std::memcpy(outlineVertices, srcVertices, vertexBytes);

    const float kOutlineExtrude = 0.001f;
    for (std::size_t vi = 0; vi < safeVertexCount; ++vi) {
        WorldVertex& v = outlineVertices[vi];
        const float lenSq = v.nx * v.nx + v.ny * v.ny + v.nz * v.nz;
        if (lenSq > 1e-10f) {
            const float invLen = 1.0f / std::sqrt(lenSq);
            v.x += v.nx * invLen * kOutlineExtrude;
            v.y += v.ny * invLen * kOutlineExtrude;
            v.z += v.nz * invLen * kOutlineExtrude;
        }
        v.r = 0.0f;
        v.g = 0.0f;
        v.b = 0.0f;
        v.a = 1.0f;
    }

    auto* outlineIndices =
        reinterpret_cast<std::uint32_t*>(worldIndexMappedData_ + outlineIndexWriteOffset);
    std::memcpy(outlineIndices, indices, indexBytes);

    D3D12_VERTEX_BUFFER_VIEW outlineVbv{};
    outlineVbv.BufferLocation =
        worldVertexBufferGpuAddress_ + static_cast<std::uint64_t>(outlineVertexWriteOffset);
    outlineVbv.StrideInBytes = worldVertexStride_;
    outlineVbv.SizeInBytes = static_cast<UINT>(vertexBytes);
    commandList_->IASetVertexBuffers(0, 1, &outlineVbv);

    D3D12_INDEX_BUFFER_VIEW outlineIbv{};
    outlineIbv.BufferLocation =
        worldIndexBufferGpuAddress_ + static_cast<std::uint64_t>(outlineIndexWriteOffset);
    outlineIbv.Format = DXGI_FORMAT_R32_UINT;
    outlineIbv.SizeInBytes = static_cast<UINT>(indexBytes);
    commandList_->IASetIndexBuffer(&outlineIbv);

    float outlineVsConstants[36] = {};
            packWorldVsConstants(
                viewProjectionMatrix4x4,
                modelMatrix,
                false,
                0u,
                0u,
                outlineVsConstants);
    std::memcpy(
        worldVsConstantMappedData_ + outlineVsConstantWriteOffset,
        outlineVsConstants,
        sizeof(outlineVsConstants));
    commandList_->SetGraphicsRootConstantBufferView(
        0,
        worldVsConstantBufferGpuAddress_ +
            static_cast<std::uint64_t>(outlineVsConstantWriteOffset));
    commandList_->SetGraphicsRootConstantBufferView(2, worldSkinMatrixBufferGpuAddress_);

    WorldPsConstants outlinePs = makeWorldPsConstants(textureData, 0.0f);
    outlinePs.materialMode = 3.0f;
    commandList_->SetGraphicsRoot32BitConstants(
        1,
        static_cast<UINT>(sizeof(WorldPsConstants) / sizeof(float)),
        &outlinePs,
        0);
    commandList_->SetPipelineState(worldPipelineState_.Get());
    ++frameIndexedD3d12PsoSets_;
    commandList_->DrawIndexedInstanced(static_cast<UINT>(safeIndexCount), instanceCount, 0, 0, 0);
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(safeIndexCount / 3u) *
                      static_cast<std::uint64_t>(instanceCount);

    worldVertexFrameOffset_ = static_cast<UINT>(outlineVertexWriteOffset + vertexBytes);
    worldIndexFrameOffset_ = static_cast<UINT>(outlineIndexWriteOffset + indexBytes);
    worldVsConstantFrameOffset_ = static_cast<UINT>(outlineVsConstantWriteOffset + 256u);
#else
    (void)mesh;
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
    (void)baseTextureDescriptorIndex;
    (void)normalTextureDescriptorIndex;
    (void)metallicRoughnessTextureDescriptorIndex;
    (void)occlusionTextureDescriptorIndex;
    (void)emissiveTextureDescriptorIndex;
    (void)envTextureDescriptorIndex;
    (void)textureData;
    (void)useTexture;
    (void)viewProjectionMatrix4x4;
    (void)instanceDataGpuAddress;
    (void)instanceCount;
    (void)surfaceWidth;
    (void)surfaceHeight;
#endif
}
