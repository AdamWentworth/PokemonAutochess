// src/engine/utils/ShaderLibrary.cpp

#include "engine/utils/ShaderLibrary.h"
#include "engine/utils/ShaderCache.h"

#include <atomic>
#include <iostream>  // std::cerr

ShaderCache* ShaderLibrary::s_cache = nullptr;

// Single fallback instance used by BOTH get() and clear().
static ShaderCache& fallbackCache() {
    static ShaderCache fallback;
    return fallback;
}

static void warnFallbackOnce() {
    static std::atomic<bool> warned{false};
    bool expected = false;
    if (warned.compare_exchange_strong(expected, true)) {
        std::cerr
            << "[ShaderLibrary][WARN] Using fallback ShaderCache because ShaderLibrary::setCache() "
               "was not called. Wire the engine-owned ShaderCache during application init.\n";
    }
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

    warnFallbackOnce();
    return fallbackCache().get(vert, frag);
}

void ShaderLibrary::clear() {
    if (s_cache) {
        s_cache->clear();
        return;
    }

    warnFallbackOnce();
    fallbackCache().clear();
}
