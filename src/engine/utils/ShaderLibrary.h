// src/engine/utils/ShaderLibrary.h
#pragma once

#include <memory>
#include <string>

class Shader;
class ShaderCache;

// ShaderLibrary is a legacy shim over an engine-owned ShaderCache.
// It should be wired during application init via setCache().
// If it is used before wiring, it will fall back to a local cache and warn once.
// (No build-type dependent aborts.)

class ShaderLibrary {
public:
    static void setCache(ShaderCache* cache);

    // KEEP 2-arg signature (matches existing call sites).
    static std::shared_ptr<Shader> get(const std::string& vert,
                                       const std::string& frag);

    static void clear();

private:
    static ShaderCache* s_cache;
};
