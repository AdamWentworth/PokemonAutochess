// ShaderLibrary.h

#pragma once
#include <memory>
#include <string>

class Shader;
class ShaderCache;

/*
    Legacy shim:
    - Keeps existing call sites working while we migrate toward EngineServices.shader cache.
    - No longer owns a global map; it delegates to an engine-owned ShaderCache.
*/
class ShaderLibrary {
public:
    static void setCache(ShaderCache* cache);

    static std::shared_ptr<Shader> get(const std::string& vert,
                                       const std::string& frag);

    static void clear();

private:
    static ShaderCache* s_cache;
};
