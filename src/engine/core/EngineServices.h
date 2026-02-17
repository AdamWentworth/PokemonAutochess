// src/engine/core/EngineServices.h
#pragma once

// Central place to thread engine-owned services into the game layer
// without exposing global singletons in game code.
//
// Batch scope: ResourceManager + ShaderCache (+ Events).
#include <string>
#include <vector>

class ResourceManager;
class ShaderCache;
class EventBus;

struct EngineFramePerfStats {
    float fps = 0.0f;
    float frameMs = 0.0f;
    float fixedMs = 0.0f;
    float renderMs = 0.0f;
    float swapMs = 0.0f;
    int fixedTicks = 0;
};

struct EngineServices {
    ResourceManager* resources = nullptr;
    ShaderCache* shaders = nullptr;

    // Engine-owned event bus (no singleton wrapper).
    EventBus* events = nullptr;

    // Updated by host loop each second (debug/perf overlay + logging).
    EngineFramePerfStats framePerf;

    // Render backend + GPU diagnostics.
    std::string requestedRendererBackend = "auto";
    std::string activeRendererBackend = "opengl";
    bool rendererBackendFallback = false;
    std::string rendererBackendFallbackReason;
    std::string gpuVendor;
    std::string gpuRenderer;
    std::vector<std::string> availableGpuAdapters;
    std::string preferredGpuAdapter;
    bool gpuDiscrete = false;
    bool requireDiscreteGpu = false;
    std::string bootMenuScreen;
};
