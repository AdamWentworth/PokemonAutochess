#include "engine/render/d3d12/D3D12TextureUpload.h"

#include <cstdint>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <d3d12.h>

namespace {

struct CpuMipLevel {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

constexpr int kGlRepeat = 10497;
constexpr int kGlClampToEdge = 33071;
constexpr int kGlMirroredRepeat = 33648;

int wrapTexelIndex(int index, int size, int wrapMode) {
    if (size <= 1) return 0;
    if (wrapMode == kGlClampToEdge) {
        if (index < 0) return 0;
        if (index >= size) return size - 1;
        return index;
    }
    if (wrapMode == kGlMirroredRepeat) {
        const int period = size * 2;
        int j = index % period;
        if (j < 0) j += period;
        if (j >= size) {
            j = period - 1 - j;
        }
        return j;
    }
    // Default to repeat for unknown modes.
    int j = index % size;
    if (j < 0) j += size;
    return j;
}

std::vector<CpuMipLevel> buildRgbaMipChain(const unsigned char* rgbaPixels,
                                           int width,
                                           int height,
                                           int wrapS,
                                           int wrapT) {
    std::vector<CpuMipLevel> chain;
    if (!rgbaPixels || width <= 0 || height <= 0) return chain;

    CpuMipLevel base;
    base.width = width;
    base.height = height;
    base.rgba.assign(
        rgbaPixels,
        rgbaPixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    chain.push_back(std::move(base));

    while (chain.back().width > 1 || chain.back().height > 1) {
        const CpuMipLevel& prev = chain.back();
        CpuMipLevel next;
        next.width = (prev.width / 2 > 0) ? (prev.width / 2) : 1;
        next.height = (prev.height / 2 > 0) ? (prev.height / 2) : 1;
        next.rgba.resize(static_cast<std::size_t>(next.width) * static_cast<std::size_t>(next.height) * 4u);

        for (int y = 0; y < next.height; ++y) {
            for (int x = 0; x < next.width; ++x) {
                std::uint32_t sumR = 0;
                std::uint32_t sumG = 0;
                std::uint32_t sumB = 0;
                std::uint32_t sumA = 0;
                std::uint32_t taps = 0;

                for (int oy = 0; oy < 2; ++oy) {
                    const int srcY = wrapTexelIndex(y * 2 + oy, prev.height, wrapT);
                    for (int ox = 0; ox < 2; ++ox) {
                        const int srcX = wrapTexelIndex(x * 2 + ox, prev.width, wrapS);
                        const std::size_t srcIndex =
                            (static_cast<std::size_t>(srcY) * static_cast<std::size_t>(prev.width) +
                             static_cast<std::size_t>(srcX)) * 4u;
                        sumR += prev.rgba[srcIndex + 0];
                        sumG += prev.rgba[srcIndex + 1];
                        sumB += prev.rgba[srcIndex + 2];
                        sumA += prev.rgba[srcIndex + 3];
                        ++taps;
                    }
                }

                const std::size_t dstIndex =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(next.width) +
                     static_cast<std::size_t>(x)) * 4u;
                const std::uint32_t safeTaps = (taps > 0) ? taps : 1u;
                next.rgba[dstIndex + 0] = static_cast<unsigned char>(sumR / safeTaps);
                next.rgba[dstIndex + 1] = static_cast<unsigned char>(sumG / safeTaps);
                next.rgba[dstIndex + 2] = static_cast<unsigned char>(sumB / safeTaps);
                next.rgba[dstIndex + 3] = static_cast<unsigned char>(sumA / safeTaps);
            }
        }

        chain.push_back(std::move(next));
    }

    return chain;
}

} // namespace

namespace engine::render::d3d12 {

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
                                   int wrapS,
                                   int wrapT,
                                   bool generateMipChain,
                                   Microsoft::WRL::ComPtr<ID3D12Resource>& outTexture) {
    if (!device || !commandQueue || !fence || !fenceEvent || !srvHeap || !rgbaPixels || width <= 0 || height <= 0) {
        return false;
    }
    std::vector<CpuMipLevel> mipChain = buildRgbaMipChain(rgbaPixels, width, height, wrapS, wrapT);
    if (mipChain.empty()) return false;
    if (!generateMipChain && mipChain.size() > 1u) {
        mipChain.resize(1u);
    }
    const UINT mipLevels = static_cast<UINT>(mipChain.size());

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
    texDesc.MipLevels = static_cast<UINT16>(mipLevels);
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

    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(mipLevels);
    std::vector<UINT> numRows(mipLevels);
    std::vector<UINT64> rowSizeBytes(mipLevels);
    UINT64 uploadBufferSize = 0;
    device->GetCopyableFootprints(&texDesc,
                                  0,
                                  mipLevels,
                                  0,
                                  footprints.data(),
                                  numRows.data(),
                                  rowSizeBytes.data(),
                                  &uploadBufferSize);
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
    auto* mappedBytes = static_cast<unsigned char*>(mapped);
    for (UINT mip = 0; mip < mipLevels; ++mip) {
        const auto& mipCpu = mipChain[mip];
        const auto& footprint = footprints[mip];
        const std::size_t srcRowPitch = static_cast<std::size_t>(mipCpu.width) * 4u;
        for (int row = 0; row < mipCpu.height; ++row) {
            auto* dstRow = mappedBytes +
                footprint.Offset +
                static_cast<std::size_t>(row) * static_cast<std::size_t>(footprint.Footprint.RowPitch);
            const auto* srcRow =
                mipCpu.rgba.data() + static_cast<std::size_t>(row) * srcRowPitch;
            std::memcpy(dstRow, srcRow, srcRowPitch);
        }
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

    for (UINT mip = 0; mip < mipLevels; ++mip) {
        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = texture.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = mip;

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = upload.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = footprints[mip];

        copyList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    }

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
    srvDesc.Texture2D.MipLevels = mipLevels;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += static_cast<SIZE_T>(descriptorIndex) * static_cast<SIZE_T>(srvDescriptorSize);
    device->CreateShaderResourceView(texture.Get(), &srvDesc, cpuHandle);

    outTexture = texture;
    return true;
}

} // namespace engine::render::d3d12
#endif
