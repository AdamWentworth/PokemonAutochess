// src/engine/utils/ShaderLibrary.h
#pragma once

#include <memory>
#include <string>

class Shader;
class ShaderCache;

// ShaderLibrary is a legacy shim over an engine-owned ShaderCache.
// Wire it during application init via setCache().
class ShaderLibrary {
public:
    static void setCache(ShaderCache* cache);

    static std::shared_ptr<Shader> get(const std::string& vert,
                                       const std::string& frag);

    static void clear();

private:
    static ShaderCache* s_cache; // kept for ABI/compat; not used for synchronization
};
