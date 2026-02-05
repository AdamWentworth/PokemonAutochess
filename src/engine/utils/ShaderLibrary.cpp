// src/engine/utils/ShaderLibrary.cpp

#include "engine/utils/ShaderLibrary.h"
#include "engine/utils/ShaderCache.h"

#include <atomic>
#include <iostream>

// Keep the original symbol for linkage.
ShaderCache* ShaderLibrary::s_cache = nullptr;

namespace {
    // Thread-safe pointer used for all operations.
    std::atomic<ShaderCache*>& cacheAtomic() {
        static std::atomic<ShaderCache*> a{nullptr};
        return a;
    }

    // Single fallback instance used by BOTH get() and clear().
    ShaderCache& fallbackCache() {
        static ShaderCache fallback;
        return fallback;
    }

    void warnFallbackOnce() {
        static std::atomic<bool> warned{false};
        bool expected = false;
        if (warned.compare_exchange_strong(expected, true)) {
            std::cerr
                << "[ShaderLibrary][WARN] Using fallback ShaderCache because ShaderLibrary::setCache() "
                   "was not called. Wire the engine-owned ShaderCache during application init.\n";
        }
    }

    ShaderCache* loadCache() {
        return cacheAtomic().load(std::memory_order_acquire);
    }
}

void ShaderLibrary::setCache(ShaderCache* cache) {
    cacheAtomic().store(cache, std::memory_order_release);
    // Best-effort: keep legacy storage in sync (only touched inside member funcs, so no access issue).
    s_cache = cache;
}

std::shared_ptr<Shader> ShaderLibrary::get(const std::string& vert,
                                           const std::string& frag)
{
    if (ShaderCache* c = loadCache()) {
        // Best-effort legacy sync for debugging reads.
        s_cache = c;
        return c->get(vert, frag);
    }

    warnFallbackOnce();
    return fallbackCache().get(vert, frag);
}

void ShaderLibrary::clear() {
    if (ShaderCache* c = loadCache()) {
        s_cache = c;
        c->clear();
        return;
    }

    warnFallbackOnce();
    fallbackCache().clear();
}
