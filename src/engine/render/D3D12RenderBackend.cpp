#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/DxgiAdapterSelection.h"
#include "engine/render/DebugGeometry.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
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
    float r;
    float g;
    float b;
    float a;
};

constexpr std::size_t kMaxSpriteQuads = 2048;
constexpr std::size_t kMaxSpriteVertices = kMaxSpriteQuads * 6;
constexpr std::size_t kMaxDebugQuads = 4096;
constexpr std::size_t kMaxDebugLines = 8192;
constexpr std::size_t kMaxDebugTriangles = 65536;
constexpr std::size_t kMaxDebugVertices = kMaxDebugTriangles * 3;
constexpr std::size_t kMaxWorldTriangles = 180000;
constexpr std::size_t kMaxWorldVertices = kMaxWorldTriangles * 3;
constexpr std::size_t kMaxSrvDescriptors = 2048;
constexpr const char* kFallbackSpriteTextureKey = "__fallback_sprite_texture__";

bool createTextureResourceFromRgba(ID3D12Device* device,
                                   ID3D12CommandQueue* commandQueue,
                                   ID3D12Fence* fence,
                                   HANDLE fenceEvent,
                                   std::uint64_t& fenceValue,
                                   ID3D12DescriptorHeap* srvHeap,
                                   std::uint32_t srvDescriptorSize,
                                   std::uint32_t descriptorIndex,
                                   const unsigned char* rgbaPixels,
                                   int width,
                                   int height,
                                   Microsoft::WRL::ComPtr<ID3D12Resource>& outTexture) {
    if (!device || !commandQueue || !fence || !fenceEvent || !srvHeap || !rgbaPixels || width <= 0 || height <= 0) {
        return false;
    }

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    defaultHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    defaultHeap.CreationNodeMask = 1;
    defaultHeap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC texDesc{};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = static_cast<UINT64>(width);
    texDesc.Height = static_cast<UINT>(height);
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    if (FAILED(device->CreateCommittedResource(&defaultHeap,
                                               D3D12_HEAP_FLAG_NONE,
                                               &texDesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST,
                                               nullptr,
                                               IID_PPV_ARGS(texture.ReleaseAndGetAddressOf()))) ||
        !texture) {
        return false;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows = 0;
    UINT64 rowSizeBytes = 0;
    UINT64 uploadBufferSize = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSizeBytes, &uploadBufferSize);
    if (uploadBufferSize == 0) return false;

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeap.CreationNodeMask = 1;
    uploadHeap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC uploadDesc{};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Alignment = 0;
    uploadDesc.Width = uploadBufferSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.SampleDesc.Quality = 0;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    if (FAILED(device->CreateCommittedResource(&uploadHeap,
                                               D3D12_HEAP_FLAG_NONE,
                                               &uploadDesc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ,
                                               nullptr,
                                               IID_PPV_ARGS(upload.ReleaseAndGetAddressOf()))) ||
        !upload) {
        return false;
    }

    void* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    if (FAILED(upload->Map(0, &readRange, &mapped)) || !mapped) {
        return false;
    }
    const std::size_t srcRowPitch = static_cast<std::size_t>(width) * 4u;
    for (int row = 0; row < height; ++row) {
        auto* dstRow = static_cast<unsigned char*>(mapped) +
            footprint.Offset +
            static_cast<std::size_t>(row) * static_cast<std::size_t>(footprint.Footprint.RowPitch);
        const auto* srcRow = rgbaPixels + static_cast<std::size_t>(row) * srcRowPitch;
        std::memcpy(dstRow, srcRow, srcRowPitch);
    }
    D3D12_RANGE writeRange{0, static_cast<SIZE_T>(uploadBufferSize)};
    upload->Unmap(0, &writeRange);

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> copyAllocator;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(copyAllocator.ReleaseAndGetAddressOf()))) ||
        !copyAllocator) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> copyList;
    if (FAILED(device->CreateCommandList(0,
                                         D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         copyAllocator.Get(),
                                         nullptr,
                                         IID_PPV_ARGS(copyList.ReleaseAndGetAddressOf()))) ||
        !copyList) {
        return false;
    }

    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource = texture.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource = upload.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = footprint;

    copyList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    D3D12_RESOURCE_BARRIER toShader{};
    toShader.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toShader.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    toShader.Transition.pResource = texture.Get();
    toShader.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toShader.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    toShader.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    copyList->ResourceBarrier(1, &toShader);

    if (FAILED(copyList->Close())) return false;

    ID3D12CommandList* lists[] = {copyList.Get()};
    commandQueue->ExecuteCommandLists(1, lists);

    const std::uint64_t signalValue = ++fenceValue;
    if (FAILED(commandQueue->Signal(fence, signalValue))) return false;
    if (fence->GetCompletedValue() < signalValue) {
        if (FAILED(fence->SetEventOnCompletion(signalValue, fenceEvent))) return false;
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += static_cast<SIZE_T>(descriptorIndex) * static_cast<SIZE_T>(srvDescriptorSize);
    device->CreateShaderResourceView(texture.Get(), &srvDesc, cpuHandle);

    outTexture = texture;
    return true;
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
    if (!worldPipelineState_ || !worldRootSignature_ || !worldVertexBuffer_ || !commandList_) return;

    const std::size_t safeCount = (triangleCount > kMaxWorldTriangles) ? kMaxWorldTriangles : triangleCount;
    if (safeCount == 0) return;
    const std::size_t vertexCount = safeCount * 3;
    const std::size_t neededBytes = vertexCount * sizeof(WorldVertex);
    if (neededBytes == 0 || neededBytes > worldVertexBufferSize_) return;

    void* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    if (FAILED(worldVertexBuffer_->Map(0, &readRange, &mapped)) || !mapped) return;

    WorldVertex* out = static_cast<WorldVertex*>(mapped);
    for (std::size_t i = 0; i < safeCount; ++i) {
        const WorldTriangle& t = triangles[i];
        const std::size_t base = i * 3;
        out[base + 0] = WorldVertex{t.x1, t.y1, t.z1, t.r, t.g, t.b, t.a};
        out[base + 1] = WorldVertex{t.x2, t.y2, t.z2, t.r, t.g, t.b, t.a};
        out[base + 2] = WorldVertex{t.x3, t.y3, t.z3, t.r, t.g, t.b, t.a};
    }
    D3D12_RANGE writeRange{0, static_cast<SIZE_T>(neededBytes)};
    worldVertexBuffer_->Unmap(0, &writeRange);

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
    commandList_->SetGraphicsRootSignature(worldRootSignature_.Get());
    commandList_->SetGraphicsRoot32BitConstants(0, 16, viewProjectionMatrix4x4, 0);
    commandList_->SetPipelineState(worldPipelineState_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = worldVertexBufferGpuAddress_;
    vbv.StrideInBytes = worldVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(neededBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->DrawInstanced(static_cast<UINT>(vertexCount), 1, 0, 0);
#else
    (void)triangles;
    (void)triangleCount;
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

    const std::size_t safeCount = (quadCount > kMaxDebugQuads) ? kMaxDebugQuads : quadCount;
    const std::size_t vertexCount = safeCount * 6;
    const std::size_t neededBytes = vertexCount * sizeof(DebugVertex);
    if (neededBytes == 0 || neededBytes > debugVertexBufferSize_) return;

    void* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    if (FAILED(debugVertexBuffer_->Map(0, &readRange, &mapped)) || !mapped) return;

    std::vector<DebugVertex> verts;
    verts.reserve(vertexCount);
    for (std::size_t i = 0; i < safeCount; ++i) {
        engine::render::debug::appendQuadAsTriangles(quads[i], verts);
    }
    if (verts.empty()) {
        debugVertexBuffer_->Unmap(0, nullptr);
        return;
    }

    DebugVertex* out = static_cast<DebugVertex*>(mapped);
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

    D3D12_RANGE writeRange{0, static_cast<SIZE_T>(clampedVertexCount * sizeof(DebugVertex))};
    debugVertexBuffer_->Unmap(0, &writeRange);

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
    vbv.BufferLocation = debugVertexBufferGpuAddress_;
    vbv.StrideInBytes = debugVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(clampedVertexCount * sizeof(DebugVertex));
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->DrawInstanced(static_cast<UINT>(clampedVertexCount), 1, 0, 0);
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

    const std::size_t safeCount = (lineCount > kMaxDebugLines) ? kMaxDebugLines : lineCount;

    std::vector<DebugVertex> verts;
    verts.reserve(safeCount * 6);
    for (std::size_t i = 0; i < safeCount; ++i) {
        engine::render::debug::appendLineAsTriangles(lines[i], verts);
    }
    if (verts.empty()) return;

    const std::size_t neededBytes = verts.size() * sizeof(DebugVertex);
    if (neededBytes == 0 || neededBytes > debugVertexBufferSize_) return;

    void* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    if (FAILED(debugVertexBuffer_->Map(0, &readRange, &mapped)) || !mapped) return;

    DebugVertex* out = static_cast<DebugVertex*>(mapped);
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

    D3D12_RANGE writeRange{0, static_cast<SIZE_T>(neededBytes)};
    debugVertexBuffer_->Unmap(0, &writeRange);

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
    vbv.BufferLocation = debugVertexBufferGpuAddress_;
    vbv.StrideInBytes = debugVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(neededBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->DrawInstanced(static_cast<UINT>(verts.size()), 1, 0, 0);
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

    const std::size_t safeCount = (triangleCount > kMaxDebugTriangles) ? kMaxDebugTriangles : triangleCount;

    std::vector<DebugVertex> verts;
    verts.reserve(safeCount * 3);
    for (std::size_t i = 0; i < safeCount; ++i) {
        const DebugTriangle& t = triangles[i];
        verts.push_back(DebugVertex{t.x1, t.y1, t.r, t.g, t.b, t.a});
        verts.push_back(DebugVertex{t.x2, t.y2, t.r, t.g, t.b, t.a});
        verts.push_back(DebugVertex{t.x3, t.y3, t.r, t.g, t.b, t.a});
    }
    if (verts.empty()) return;

    const std::size_t neededBytes = verts.size() * sizeof(DebugVertex);
    if (neededBytes == 0 || neededBytes > debugVertexBufferSize_) return;

    void* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    if (FAILED(debugVertexBuffer_->Map(0, &readRange, &mapped)) || !mapped) return;

    DebugVertex* out = static_cast<DebugVertex*>(mapped);
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
    D3D12_RANGE writeRange{0, static_cast<SIZE_T>(neededBytes)};
    debugVertexBuffer_->Unmap(0, &writeRange);

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
    vbv.BufferLocation = debugVertexBufferGpuAddress_;
    vbv.StrideInBytes = debugVertexStride_;
    vbv.SizeInBytes = static_cast<UINT>(neededBytes);
    commandList_->IASetVertexBuffers(0, 1, &vbv);
    commandList_->DrawInstanced(static_cast<UINT>(verts.size()), 1, 0, 0);
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

    const std::size_t safeCount = (spriteCount > kMaxSpriteQuads) ? kMaxSpriteQuads : spriteCount;

    std::vector<SpriteVertex> vertices;
    vertices.reserve(safeCount * 6);
    std::vector<std::uint32_t> descriptorIndices;
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
    if (neededBytes == 0 || neededBytes > spriteVertexBufferSize_) return;

    void* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    if (FAILED(spriteVertexBuffer_->Map(0, &readRange, &mapped)) || !mapped) return;
    std::memcpy(mapped, vertices.data(), neededBytes);
    D3D12_RANGE writeRange{0, static_cast<SIZE_T>(neededBytes)};
    spriteVertexBuffer_->Unmap(0, &writeRange);

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
    vbv.BufferLocation = spriteVertexBufferGpuAddress_;
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
    debugVertexBuffer_.Reset();
    debugVertexBufferGpuAddress_ = 0;
    debugVertexStride_ = 0;
    debugVertexBufferSize_ = 0;
    worldVertexBuffer_.Reset();
    worldVertexBufferGpuAddress_ = 0;
    worldVertexStride_ = 0;
    worldVertexBufferSize_ = 0;
    worldPipelineState_.Reset();
    worldRootSignature_.Reset();
    spriteTextures_.clear();
    spriteVertexBuffer_.Reset();
    spriteVertexBufferGpuAddress_ = 0;
    spriteVertexStride_ = 0;
    spriteVertexBufferSize_ = 0;
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
#endif
}

void D3D12RenderBackend::createWorldPipeline() {
#if defined(_WIN32)
    static constexpr char kVsSource[] =
        "cbuffer VSConstants : register(b0) { float4x4 uViewProj; };"
        "struct VSIn { float3 pos : POSITION; float4 col : COLOR; };"
        "struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR; };"
        "VSOut main(VSIn i) {"
        "  VSOut o;"
        "  o.pos = mul(uViewProj, float4(i.pos, 1.0f));"
        "  o.col = i.col;"
        "  return o;"
        "}";
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
        throw std::runtime_error("D3DCompile failed for world VS.");
    }
    errBlob.Reset();
    if (FAILED(D3DCompile(kPsSource, sizeof(kPsSource) - 1, nullptr, nullptr, nullptr,
                          "main", "ps_5_0", 0, 0, psBlob.ReleaseAndGetAddressOf(),
                          errBlob.ReleaseAndGetAddressOf())) ||
        !psBlob) {
        throw std::runtime_error("D3DCompile failed for world PS.");
    }

    D3D12_ROOT_PARAMETER rootParam{};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParam.Constants.Num32BitValues = 16;
    rootParam.Constants.ShaderRegister = 0;
    rootParam.Constants.RegisterSpace = 0;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 1;
    rsDesc.pParameters = &rootParam;
    rsDesc.NumStaticSamplers = 0;
    rsDesc.pStaticSamplers = nullptr;
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
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
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
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
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
    if (!createTextureResourceFromRgba(device_.Get(),
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
    unsigned char* pixels = stbi_load(texturePath.c_str(), &width, &height, &channels, 4);
    std::string altPath;
    if (!pixels) {
        altPath = texturePath;
        std::replace(altPath.begin(), altPath.end(), '\\', '/');
        if (altPath != texturePath) {
            pixels = stbi_load(altPath.c_str(), &width, &height, &channels, 4);
        }
    }

    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        return ensureFallbackSpriteTexture();
    }

    SpriteTexture texture;
    texture.descriptorIndex = nextSrvDescriptorIndex_;
    const bool ok = createTextureResourceFromRgba(device_.Get(),
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
