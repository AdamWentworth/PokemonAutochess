// src/engine/core/EngineServices.h
#pragma once

// Central place to thread engine-owned services into the game layer
// without exposing global singletons in game code.
//
// Batch 1 scope: ResourceManager + SystemRegistry only (more later).

class SystemRegistry;
class ResourceManager;
class ShaderCache;

struct EngineServices {
    SystemRegistry* systems  = nullptr;
    ResourceManager* resources = nullptr;
    ShaderCache* shaders = nullptr;
};
