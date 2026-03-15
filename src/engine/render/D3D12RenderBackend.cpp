#include "engine/render/D3D12RenderBackend.h"
#include "engine/render/DxgiAdapterSelection.h"
#include "engine/render/DebugGeometry.h"
#include "engine/render/RendererParityContract.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"
#include "engine/core/Environment.h"

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
                                       bool vsyncEnabled,
                                       const std::string& preferredAdapterName)
    : window_(window)
    , width_((width > 1) ? width : 1)
    , height_((height > 1) ? height : 1)
    , vsyncEnabled_(vsyncEnabled) {
    if (!window_) {
        throw std::runtime_error("D3D12RenderBackend requires a valid SDL_Window.");
    }
    initDeviceAndSwapchain(preferredAdapterName);
    engine::render::parity_contract::RuntimeConfig parityCfg =
        engine::render::parity_contract::makeBaselineConfig();
    parityCfg.framebufferSrgbEnabled = false;
    engine::render::parity_contract::logValidation("D3D12", parityCfg);
    configureScreenshotCapture();
}

D3D12RenderBackend::~D3D12RenderBackend() {
    shutdown();
}

bool D3D12RenderBackend::getLastFrameTimings(BackendFrameTimings& outTimings) const {
    outTimings.presentWaitMs = lastPresentWaitMs_;
    outTimings.gpuFrameMs = lastGpuFrameMs_;
    outTimings.gpuFrameValid = lastGpuFrameValid_;
    return true;
}

bool D3D12RenderBackend::getLastFrameStats(BackendFrameStats& outStats) const {
    outStats = BackendFrameStats{};
    outStats.drawCalls = lastFrameDrawCalls_;
    outStats.triangles = lastFrameTriangles_;
    outStats.indexedOpaqueDraws = lastFrameIndexedOpaqueDraws_;
    outStats.indexedBlendDraws = lastFrameIndexedBlendDraws_;
    outStats.indexedCachedDraws = lastFrameIndexedCachedDraws_;
    outStats.indexedDynamicDraws = lastFrameIndexedDynamicDraws_;
    outStats.indexedInstancedDraws = lastFrameIndexedInstancedDraws_;
    outStats.indexedOutlineBatches = lastFrameIndexedOutlineBatches_;
    outStats.indexedGeometrySwitches = lastFrameIndexedGeometrySwitches_;
    outStats.indexedMaterialSwitches = lastFrameIndexedMaterialSwitches_;
    outStats.indexedTextureSwitches = lastFrameIndexedTextureSwitches_;
    outStats.indexedD3d12PsoSets = lastFrameIndexedD3d12PsoSets_;
    outStats.indexedD3d12DescriptorTableSets =
        lastFrameIndexedD3d12DescriptorTableSets_;
    return true;
}

void D3D12RenderBackend::beginWorldIndexedBatchSubmission() {
#if defined(_WIN32)
    auto& state = worldIndexedBatchSubmissionState_;
    if (state.active) {
        ++state.depth;
        return;
    }
    state = WorldIndexedBatchSubmissionState{};
    state.active = true;
    state.depth = 1u;
#endif
}

void D3D12RenderBackend::endWorldIndexedBatchSubmission() {
#if defined(_WIN32)
    auto& state = worldIndexedBatchSubmissionState_;
    if (!state.active) {
        return;
    }
    if (state.depth > 1u) {
        --state.depth;
        return;
    }
    state = WorldIndexedBatchSubmissionState{};
#endif
}

void D3D12RenderBackend::recordWorldIndexedSubmissionStats(
    const WorldIndexedSubmissionStats& stats) {
    frameIndexedOpaqueDraws_ += stats.opaqueDraws;
    frameIndexedBlendDraws_ += stats.blendDraws;
    frameIndexedCachedDraws_ += stats.cachedDraws;
    frameIndexedDynamicDraws_ += stats.dynamicDraws;
    frameIndexedInstancedDraws_ += stats.instancedDraws;
    frameIndexedOutlineBatches_ += stats.outlineBatches;
    frameIndexedGeometrySwitches_ += stats.geometrySwitches;
    frameIndexedMaterialSwitches_ += stats.materialSwitches;
    frameIndexedTextureSwitches_ += stats.textureSwitches;
}

#if defined(_WIN32)
void D3D12RenderBackend::bindWorldIndexedCommonState(int surfaceWidth, int surfaceHeight) {
    if (!commandList_ || !srvHeap_ || !worldRootSignature_) {
        return;
    }

    auto& state = worldIndexedBatchSubmissionState_;
    const bool reuseBindings = state.active;
    if (!reuseBindings ||
        !state.viewportScissorBound ||
        state.boundSurfaceWidth != surfaceWidth ||
        state.boundSurfaceHeight != surfaceHeight) {
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
        if (reuseBindings) {
            state.viewportScissorBound = true;
            state.boundSurfaceWidth = surfaceWidth;
            state.boundSurfaceHeight = surfaceHeight;
        }
    }

    bool invalidateDescriptorTables = false;
    if (!reuseBindings || !state.srvHeapBound) {
        ID3D12DescriptorHeap* heaps[] = {srvHeap_.Get()};
        commandList_->SetDescriptorHeaps(1, heaps);
        if (reuseBindings) {
            state.srvHeapBound = true;
            invalidateDescriptorTables = true;
        }
    }

    if (!reuseBindings || !state.rootSignatureBound) {
        commandList_->SetGraphicsRootSignature(worldRootSignature_.Get());
        if (reuseBindings) {
            state.rootSignatureBound = true;
            invalidateDescriptorTables = true;
        }
    }

    if (reuseBindings && invalidateDescriptorTables) {
        state.currentDescriptorIndices.fill(0xffffffffu);
    }
}

void D3D12RenderBackend::bindWorldIndexedDescriptorTables(
    std::uint32_t baseTextureDescriptorIndex,
    std::uint32_t normalTextureDescriptorIndex,
    std::uint32_t metallicRoughnessTextureDescriptorIndex,
    std::uint32_t occlusionTextureDescriptorIndex,
    std::uint32_t emissiveTextureDescriptorIndex,
    std::uint32_t envTextureDescriptorIndex) {
    if (!commandList_ || !srvHeap_) {
        return;
    }

    const std::uint32_t descriptorIndices[6] = {
        baseTextureDescriptorIndex,
        normalTextureDescriptorIndex,
        metallicRoughnessTextureDescriptorIndex,
        occlusionTextureDescriptorIndex,
        emissiveTextureDescriptorIndex,
        envTextureDescriptorIndex};
    auto& state = worldIndexedBatchSubmissionState_;
    const bool reuseBindings = state.active;
    const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < 6u; ++i) {
        if (reuseBindings && state.currentDescriptorIndices[i] == descriptorIndices[i]) {
            continue;
        }
        D3D12_GPU_DESCRIPTOR_HANDLE handle = heapStart;
        handle.ptr += static_cast<SIZE_T>(descriptorIndices[i]) *
                      static_cast<SIZE_T>(srvDescriptorSize_);
        commandList_->SetGraphicsRootDescriptorTable(3u + i, handle);
        ++frameIndexedD3d12DescriptorTableSets_;
        if (reuseBindings) {
            state.currentDescriptorIndices[i] = descriptorIndices[i];
        }
    }
}

void D3D12RenderBackend::bindWorldIndexedPipelineState(ID3D12PipelineState* pso) {
    if (!commandList_ || !pso) {
        return;
    }

    auto& state = worldIndexedBatchSubmissionState_;
    if (state.active && state.currentPso == pso) {
        return;
    }

    commandList_->SetPipelineState(pso);
    ++frameIndexedD3d12PsoSets_;
    if (state.active) {
        state.currentPso = pso;
    }
}

void D3D12RenderBackend::bindWorldIndexedPrimitiveTopology() {
    if (!commandList_) {
        return;
    }

    auto& state = worldIndexedBatchSubmissionState_;
    constexpr std::uint32_t kTriangleListTopology =
        static_cast<std::uint32_t>(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (state.active &&
        state.primitiveTopologyBound &&
        state.currentPrimitiveTopology == kTriangleListTopology) {
        return;
    }

    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (state.active) {
        state.primitiveTopologyBound = true;
        state.currentPrimitiveTopology = kTriangleListTopology;
    }
}
#endif

void D3D12RenderBackend::configureScreenshotCapture() {
    const auto path = engine::env::get("PAC_BACKEND_SCREENSHOT_PATH");
    if (!path.has_value() || path->empty()) return;

    screenshotPath_ = *path;
    screenshotCaptureConfigured_ = true;
    screenshotCaptured_ = false;
    frameCounter_ = 0u;
    screenshotFrameTarget_ = 0u;

    if (const auto frame = engine::env::get("PAC_BACKEND_SCREENSHOT_FRAME")) {
        try {
            screenshotFrameTarget_ = static_cast<std::uint64_t>(std::stoull(*frame));
        } catch (...) {
            screenshotFrameTarget_ = 0u;
        }
    }
}

