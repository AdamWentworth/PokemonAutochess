// src/engine/core/EngineServices.h
#pragma once

// Central place to thread engine-owned services into the game layer
// without exposing global singletons in game code.
//
// Batch scope: ResourceManager + ShaderCache (+ Events).
#include <cstdint>
#include <string>
#include <vector>

class ResourceManager;
class ShaderCache;
class EventBus;

struct EngineFramePerfStats {
    float fps = 0.0f;
    float frameMs = 0.0f;
    float fixedMs = 0.0f;
    float renderBuildMs = 0.0f;
    float renderSubmitMs = 0.0f;
    float presentWaitMs = 0.0f;
    float gpuFrameMs = 0.0f;
    bool gpuFrameValid = false;
    std::uint32_t drawCalls = 0u;
    std::uint64_t triangles = 0u;
    std::uint32_t visibleAnimatedUnits = 0u;
    std::uint32_t particleCount = 0u;
    float projectedUnitsMs = 0.0f;
    float projectedPoseEvalMs = 0.0f;
    float projectedModelMs = 0.0f;
    float projectedOverlayMs = 0.0f;
    std::uint32_t projectedUnitsProcessed = 0u;
    std::uint32_t projectedModelUnits = 0u;
    std::uint32_t projectedClipSkinnedUnits = 0u;
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
    // Updated by runtime each frame; host loop samples and aggregates.
    std::uint32_t frameVisibleAnimatedUnits = 0u;
    std::uint32_t frameParticleCount = 0u;
    float frameProjectedUnitsMs = 0.0f;
    float frameProjectedPoseEvalMs = 0.0f;
    float frameProjectedModelMs = 0.0f;
    float frameProjectedOverlayMs = 0.0f;
    std::uint32_t frameProjectedUnitsProcessed = 0u;
    std::uint32_t frameProjectedModelUnits = 0u;
    std::uint32_t frameProjectedClipSkinnedUnits = 0u;

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
    bool characterInkingEnabled = false;
    std::string bootMenuScreen;
};
