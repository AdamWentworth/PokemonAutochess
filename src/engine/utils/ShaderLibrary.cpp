// ShaderLibrary.cpp

#include "engine/utils/ShaderLibrary.h"
#include "engine/utils/ShaderCache.h"

#include <atomic>
#include <cstdlib>   // std::abort
#include <iostream>  // std::cerr

ShaderCache* ShaderLibrary::s_cache = nullptr;

// Single fallback instance used by BOTH get() and clear().
static ShaderCache& fallbackCache() {
    static ShaderCache fallback;
    return fallback;
}

static void warnFallbackOnce() {
#if PAC_ALLOW_SHADERLIB_FALLBACK
    static std::atomic<bool> warned{false};
    bool expected = false;
    if (warned.compare_exchange_strong(expected, true)) {
        std::cerr
            << "[ShaderLibrary][WARN] Using fallback ShaderCache because ShaderLibrary::setCache() "
               "was not called. This should be wired during application init.\n";
    }
#endif
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

#if PAC_ALLOW_SHADERLIB_FALLBACK
    warnFallbackOnce();
    return fallbackCache().get(vert, frag);
#else
    // Fail fast in Release if services weren't wired correctly.
    std::cerr
        << "[ShaderLibrary][FATAL] ShaderLibrary::get() called before ShaderLibrary::setCache().\n"
        << "  vert: " << vert << "\n"
        << "  frag: " << frag << "\n";
    std::abort();
#endif
}

void ShaderLibrary::clear() {
    if (s_cache) {
        s_cache->clear();
        return;
    }

#if PAC_ALLOW_SHADERLIB_FALLBACK
    warnFallbackOnce();
    fallbackCache().clear();
#else
    // Safe no-op in Release if nothing was wired (avoid crashing during teardown order changes).
    return;
#endif
}
