#include <string>
#include <unordered_map>
#include <vector>

#include "engine/render/IRenderBackend.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/session/SessionGrowlPrewarm.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"

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
    }

    std::vector<std::string> prewarmedTextureKeys;
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

bool hasGrowlTextureKeyPrefix(const std::vector<std::string>& keys) {
    for (const std::string& key : keys) {
        if (key.find("growl:growl_eid_") == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

bool test_session_growl_prewarm_contract(std::string& outFail) {
    RecordingBackend backend;
    std::unordered_map<std::string, game::runtime::SharedBackendTextureCacheEntry> cache;
    game::runtime::render_model::MeshData mesh = makeTriangleMesh();

    const auto stats = game::runtime::session_growl_prewarm::prewarm(
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
        outFail = "SessionGrowlPrewarm should build growl batches, bake pass textures, and prewarm renderer batches.";
        return false;
    }

    if (stats.warmedBatches != backend.prewarmedTextureKeys.size()) {
        outFail = "SessionGrowlPrewarm should prewarm one backend texture payload per generated growl batch.";
        return false;
    }

    if (!hasGrowlTextureKeyPrefix(backend.prewarmedTextureKeys)) {
        outFail = "SessionGrowlPrewarm should prewarm growl batch texture keys rather than only raw source textures.";
        return false;
    }

    if (cache.find("__growl_baked:growl_eid_1076:m:assets/textures/moves/growl/Texture3918.png") == cache.end()) {
        outFail = "SessionGrowlPrewarm should populate baked growl texture entries in the shared backend texture cache.";
        return false;
    }

    return true;
}
