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
                                       const std::string& preferredAdapterName)
    : window_(window)
    , width_((width > 1) ? width : 1)
    , height_((height > 1) ? height : 1) {
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
    outStats.drawCalls = lastFrameDrawCalls_;
    outStats.triangles = lastFrameTriangles_;
    return true;
}

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

