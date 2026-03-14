#include <string>
#include <unordered_map>
#include <vector>

#include "engine/render/IRenderBackend.h"
#include "game/runtime/startup/RuntimeParticleVfxPrewarm.h"
#include "game/runtime/shared/vfx/particles/SharedParticleSnapshotBillboards.h"

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
        if (!texture || !texture->key || !texture->cacheKey) {
            return;
        }
        prewarmedTextureKeys.emplace_back(texture->key);
        prewarmedTextureCacheKeys.emplace_back(texture->cacheKey);
    }

    std::vector<std::string> prewarmedTextureKeys;
    std::vector<std::string> prewarmedTextureCacheKeys;
};

game::runtime::SharedBackendTextureCacheEntry makeTexture() {
    game::runtime::SharedBackendTextureCacheEntry out;
    out.attemptedLoad = true;
    out.valid = true;
    out.width = 2;
    out.height = 2;
    out.rgba.resize(16u, 255u);
    return out;
}

bool allSharedParticleCacheKeys(const std::vector<std::string>& keys) {
    for (const std::string& key : keys) {
        if (key.rfind("__particle_shared__:", 0) != 0) {
            return false;
        }
    }
    return !keys.empty();
}

} // namespace

bool test_runtime_particle_vfx_prewarm_contract(std::string& outFail) {
    RecordingBackend backend;
    std::unordered_map<std::string, game::runtime::SharedBackendTextureCacheEntry> cache;

    const auto stats = game::runtime::particle_vfx_prewarm::prewarm(
        {
            .renderer = &backend,
            .ensureBackendTextureLoaded =
                [&](const std::string& texturePath, bool) -> game::runtime::SharedBackendTextureCacheEntry* {
                    auto it = cache.find(texturePath);
                    if (it == cache.end()) {
                        it = cache.emplace(texturePath, makeTexture()).first;
                    }
                    return &it->second;
                },
        });

    const std::size_t expectedCount =
        game::runtime::shared_particle_snapshot_billboards::commonParticleTexturePaths().size();
    if (stats.textures != expectedCount || stats.warmedBatches != expectedCount) {
        outFail = "RuntimeParticleVfxPrewarm should warm every common shared particle texture.";
        return false;
    }

    if (backend.prewarmedTextureKeys.size() != expectedCount ||
        backend.prewarmedTextureCacheKeys.size() != expectedCount) {
        outFail = "RuntimeParticleVfxPrewarm should prewarm one backend texture payload per shared particle texture.";
        return false;
    }

    if (!allSharedParticleCacheKeys(backend.prewarmedTextureCacheKeys)) {
        outFail = "RuntimeParticleVfxPrewarm should use shared particle cache keys for backend texture uploads.";
        return false;
    }

    return true;
}
