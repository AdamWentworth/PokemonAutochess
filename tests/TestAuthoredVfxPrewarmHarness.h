#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "engine/render/IRenderBackend.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"

namespace test_authored_vfx_prewarm_harness {

class RecordingBackend final : public IRenderBackend {
public:
    const char* backendId() const override { return "test"; }
    void beginFrame(float, float, float, float) override {}
    void endFrame() override {}
    void onResize(int, int) override {}
    bool requiresOpenGLContext() const override { return false; }
    bool handlesPresentation() const override { return false; }
    void shutdown() override {}
    bool supportsWorldIndexedMeshes() const override { return true; }

    void prewarmWorldIndexedMeshCached(const char* geometryKey,
                                       const WorldMeshVertex*,
                                       std::size_t,
                                       const std::uint32_t*,
                                       std::size_t) override {
        if (geometryKey) {
            prewarmedGeometryKeys.emplace_back(geometryKey);
        }
    }

    void prewarmWorldIndexedMeshInstances(std::size_t instanceCount) override {
        prewarmedInstanceCounts.push_back(instanceCount);
    }

    void prewarmWorldTextureData(const WorldTextureData* texture) override {
        if (texture && texture->key) {
            prewarmedTextureKeys.emplace_back(texture->key);
        }
        if (texture && texture->cacheKey) {
            prewarmedTextureCacheKeys.emplace_back(texture->cacheKey);
        }
    }

    std::vector<std::string> prewarmedTextureKeys;
    std::vector<std::string> prewarmedTextureCacheKeys;
    std::vector<std::string> prewarmedGeometryKeys;
    std::vector<std::size_t> prewarmedInstanceCounts;
};

inline game::runtime::render_model::MeshData makeTriangleMesh() {
    game::runtime::render_model::MeshData mesh;
    mesh.vertices.resize(3u);
    mesh.vertices[0].position = glm::vec3(0.0f, 0.0f, 0.0f);
    mesh.vertices[0].uv = glm::vec2(0.0f, 0.0f);
    mesh.vertices[0].color = glm::vec4(1.0f);
    mesh.vertices[1].position = glm::vec3(1.0f, 0.0f, 0.0f);
    mesh.vertices[1].uv = glm::vec2(1.0f, 0.0f);
    mesh.vertices[1].color = glm::vec4(1.0f);
    mesh.vertices[2].position = glm::vec3(0.0f, 0.0f, 1.0f);
    mesh.vertices[2].uv = glm::vec2(0.0f, 1.0f);
    mesh.vertices[2].color = glm::vec4(1.0f);
    mesh.indices = {0u, 1u, 2u};
    return mesh;
}

inline game::runtime::SharedBackendTextureCacheEntry makeTexture(int width, int height) {
    game::runtime::SharedBackendTextureCacheEntry out;
    out.attemptedLoad = true;
    out.valid = true;
    out.width = width;
    out.height = height;
    out.rgba.resize(static_cast<std::size_t>(width * height * 4), 255u);
    return out;
}

inline bool anyKeyHasPrefix(const std::vector<std::string>& keys, const std::string& prefix) {
    for (const std::string& key : keys) {
        if (key.find(prefix) == 0u) {
            return true;
        }
    }
    return false;
}

inline bool cacheContainsPrefix(
    const std::unordered_map<std::string, game::runtime::SharedBackendTextureCacheEntry>& cache,
    const std::string& prefix) {
    for (const auto& [key, value] : cache) {
        (void)value;
        if (key.find(prefix) == 0u) {
            return true;
        }
    }
    return false;
}

template <typename TArgs>
TArgs makeArgs(RecordingBackend& backend,
               std::unordered_map<std::string, game::runtime::SharedBackendTextureCacheEntry>& cache,
               game::runtime::render_model::MeshData& mesh) {
    return TArgs{
        .renderer = &backend,
        .backendTextureByPath = &cache,
        .ensureBackendMeshLoaded =
            [&](const std::string&) -> game::runtime::render_model::MeshData* {
                return &mesh;
            },
        .ensureBackendTextureLoaded =
            [&](const std::string& texturePath, bool) -> game::runtime::SharedBackendTextureCacheEntry* {
                const std::string key = texturePath.empty() ? "__white__" : texturePath;
                auto it = cache.find(key);
                if (it == cache.end()) {
                    it = cache.emplace(key, makeTexture(2, 2)).first;
                }
                return &it->second;
            },
    };
}

} // namespace test_authored_vfx_prewarm_harness
