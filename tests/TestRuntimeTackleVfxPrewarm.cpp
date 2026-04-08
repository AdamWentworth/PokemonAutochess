#include <string>
#include <unordered_map>
#include <vector>

#include "engine/render/IRenderBackend.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/startup/RuntimeTackleVfxPrewarm.h"

namespace {

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
};

game::runtime::render_model::MeshData makeTriangleMesh() {
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

game::runtime::SharedBackendTextureCacheEntry makeTexture(int width, int height) {
    game::runtime::SharedBackendTextureCacheEntry out;
    out.attemptedLoad = true;
    out.valid = true;
    out.width = width;
    out.height = height;
    out.rgba.resize(static_cast<std::size_t>(width * height * 4), 255u);
    return out;
}

bool hasTackleTextureKeyPrefix(const std::vector<std::string>& keys) {
    for (const std::string& key : keys) {
        if (key.find("authored_vfx:tackle_eid_") == 0) {
            return true;
        }
    }
    return false;
}

bool hasTackleTextureCacheKeyPrefix(const std::vector<std::string>& keys) {
    for (const std::string& key : keys) {
        if (key.find("__authored_vfx_") == 0) {
            return true;
        }
    }
    return false;
}

bool cacheContainsPrefix(const std::unordered_map<std::string, game::runtime::SharedBackendTextureCacheEntry>& cache,
                         const std::string& prefix) {
    for (const auto& [key, value] : cache) {
        (void)value;
        if (key.find(prefix) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

bool test_runtime_tackle_vfx_prewarm_contract(std::string& outFail) {
    RecordingBackend backend;
    std::unordered_map<std::string, game::runtime::SharedBackendTextureCacheEntry> cache;
    game::runtime::render_model::MeshData mesh = makeTriangleMesh();

    const auto stats = game::runtime::tackle_vfx_prewarm::prewarm(
        {
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
        });

    if (stats.drawPasses == 0u || stats.bakedTextures == 0u || stats.warmedBatches == 0u) {
        outFail = "RuntimeTackleVfxPrewarm should build tackle batches, bake pass textures, and prewarm renderer batches.";
        return false;
    }

    if (stats.warmedBatches != backend.prewarmedTextureKeys.size()) {
        outFail = "RuntimeTackleVfxPrewarm should prewarm one backend texture payload per generated tackle batch.";
        return false;
    }

    if (!hasTackleTextureKeyPrefix(backend.prewarmedTextureKeys)) {
        outFail = "RuntimeTackleVfxPrewarm should prewarm tackle batch texture keys rather than only raw source textures.";
        return false;
    }

    if (backend.prewarmedTextureCacheKeys.size() != stats.warmedBatches ||
        !hasTackleTextureCacheKeyPrefix(backend.prewarmedTextureCacheKeys)) {
        outFail = "RuntimeTackleVfxPrewarm should prewarm stable tackle texture cache keys for backend reuse.";
        return false;
    }

    if (!cacheContainsPrefix(cache, "__authored_vfx_baked:tackle_eid_")) {
        outFail = "RuntimeTackleVfxPrewarm should populate baked tackle texture entries in the shared backend texture cache.";
        return false;
    }

    return true;
}
