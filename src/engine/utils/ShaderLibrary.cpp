// src/engine/utils/ShaderLibrary.cpp

#include "engine/utils/ShaderLibrary.h"
#include "engine/utils/ShaderCache.h"

#include <atomic>
#include <iostream>
#include <stdexcept>

ShaderCache* ShaderLibrary::s_cache = nullptr;

namespace {
    // Single source of truth for the cache pointer; atomic avoids data races if accessed cross-thread.
    std::atomic<ShaderCache*>& cacheAtomic() {
        static std::atomic<ShaderCache*> a{nullptr};
        return a;
    }

    [[noreturn]] void failUnwired(const char* where) {
        std::cerr
            << "[ShaderLibrary][ERROR] ShaderLibrary::setCache() was not called before "
            << where
            << ". This indicates an initialization-order or lifetime bug.\n";
        throw std::runtime_error("ShaderLibrary used before setCache()");
    }

    ShaderCache* loadCache() {
        return cacheAtomic().load(std::memory_order_acquire);
    }
}

void ShaderLibrary::setCache(ShaderCache* cache) {
    cacheAtomic().store(cache, std::memory_order_release);

    // Keep the legacy pointer for debugging/observability, but do not use it for correctness.
    s_cache = cache;
}

std::shared_ptr<Shader> ShaderLibrary::get(const std::string& vert,
                                           const std::string& frag) {
    ShaderCache* c = loadCache();
    if (!c) failUnwired("ShaderLibrary::get()");
    return c->get(vert, frag);
}

void ShaderLibrary::clear() {
    ShaderCache* c = loadCache();
    if (!c) failUnwired("ShaderLibrary::clear()");
    c->clear();
}
