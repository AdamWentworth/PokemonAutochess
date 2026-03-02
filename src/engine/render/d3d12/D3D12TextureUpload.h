#pragma once

#include <cstdint>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl/client.h>

struct ID3D12CommandQueue;
struct ID3D12DescriptorHeap;
struct ID3D12Device;
struct ID3D12Fence;
struct ID3D12Resource;

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
                                   bool srgbColorData,
                                   Microsoft::WRL::ComPtr<ID3D12Resource>& outTexture);

} // namespace engine::render::d3d12
#endif
