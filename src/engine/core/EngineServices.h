// src/engine/core/EngineServices.h
#pragma once

// Central place to thread engine-owned services into the game layer
// without exposing global singletons in game code.
//
// Batch scope: ResourceManager + ShaderCache (+ Events).
class ResourceManager;
class ShaderCache;
class EventBus;

struct EngineServices {
    ResourceManager* resources = nullptr;
    ShaderCache* shaders = nullptr;

    // Engine-owned event bus (no singleton wrapper).
    EventBus* events = nullptr;
};
