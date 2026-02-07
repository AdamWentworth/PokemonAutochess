// src/engine/core/EngineServices.h
#pragma once

// Central place to thread engine-owned services into the game layer
// without exposing global singletons in game code.
//
// Batch 1 scope: ResourceManager + SystemRegistry + ShaderCache (+ Events).

class SystemRegistry;
class ResourceManager;
class ShaderCache;
class EventBus;

struct EngineServices {
    SystemRegistry* systems  = nullptr;
    ResourceManager* resources = nullptr;
    ShaderCache* shaders = nullptr;

    // Engine-owned event bus (no singleton wrapper).
    EventBus* events = nullptr;
};
