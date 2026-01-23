// ShaderLibrary.h

#pragma once
#include <memory>
#include <string>

class Shader;
class ShaderCache;

// Default behavior:
// - Debug: allow fallback + warn once
// - Release: disable fallback; fail fast if not initialized
#ifndef PAC_ALLOW_SHADERLIB_FALLBACK
    #if !defined(NDEBUG)
        #define PAC_ALLOW_SHADERLIB_FALLBACK 1
    #else
        #define PAC_ALLOW_SHADERLIB_FALLBACK 0
    #endif
#endif

/*
    Legacy shim:
    - Keeps existing call sites working while we migrate toward EngineServices.shader cache.
    - Delegates to an engine-owned ShaderCache.
*/
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
