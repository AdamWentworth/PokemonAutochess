// src/engine/utils/ShaderLibrary.h
#pragma once

#include <memory>
#include <string>

class Shader;
class ShaderCache;

// ShaderLibrary is a legacy shim over an engine-owned ShaderCache.
// Wire it during application init via setCache().
//
// This drop-in replacement keeps the same API surface for call sites
// but makes access thread-safe and deterministic (atomic cache pointer).
class ShaderLibrary {
public:
    static void setCache(ShaderCache* cache);

    // KEEP 2-arg signature (matches existing call sites).
    static std::shared_ptr<Shader> get(const std::string& vert,
                                       const std::string& frag);

    static void clear();

private:
    // Kept for compatibility with any existing references/symbol expectations.
    // Internally, we also maintain an atomic pointer used for thread-safe access.
    static ShaderCache* s_cache;
};
