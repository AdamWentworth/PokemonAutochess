// ShaderLibrary.cpp

#include "engine/utils/ShaderLibrary.h"
#include "engine/utils/ShaderCache.h"

ShaderCache* ShaderLibrary::s_cache = nullptr;

// Single fallback instance used by BOTH get() and clear().
static ShaderCache& fallbackCache() {
    static ShaderCache fallback;
    return fallback;
}

void ShaderLibrary::setCache(ShaderCache* cache) {
    s_cache = cache;
}

std::shared_ptr<Shader> ShaderLibrary::get(const std::string& vert,
                                           const std::string& frag)
{
    if (s_cache) {
        return s_cache->get(vert, frag);
    }

    // Safety fallback (should not happen once Application wires services):
    return fallbackCache().get(vert, frag);
}

void ShaderLibrary::clear() {
    if (s_cache) {
        s_cache->clear();
        return;
    }
    fallbackCache().clear();
}
