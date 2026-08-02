#include <string>
#include <vector>

#include "engine/render/IRenderBackend.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/session/SessionBackendRenderHelpers.h"

namespace {

class RecordingBackend final : public IRenderBackend {
public:
    struct TextureCall {
        std::string key;
        std::string cacheKey;
        std::string normalKey;
        std::string emissiveKey;
    };

    const char* backendId() const override { return "test"; }
    void beginFrame(float, float, float, float) override {}
    void endFrame() override {}
    void onResize(int, int) override {}
    bool requiresOpenGLContext() const override { return false; }
    bool handlesPresentation() const override { return false; }
    void shutdown() override {}

    void prewarmWorldTextureData(const WorldTextureData* texture) override {
        TextureCall call;
        if (texture) {
            if (texture->key) call.key = texture->key;
            if (texture->cacheKey) call.cacheKey = texture->cacheKey;
            if (texture->normalKey) call.normalKey = texture->normalKey;
            if (texture->emissiveKey) call.emissiveKey = texture->emissiveKey;
        }
        calls.push_back(std::move(call));
    }

    std::vector<TextureCall> calls;
};

game::runtime::render_model::CachedTextureRgba makeTexture(int width, int height) {
    game::runtime::render_model::CachedTextureRgba tex;
    tex.width = width;
    tex.height = height;
    tex.rgba.resize(static_cast<std::size_t>(width * height * 4), 255u);
    return tex;
}

} // namespace

bool test_session_backend_render_helpers_contract(std::string& outFail) {
    using game::runtime::session_backend_render_helpers::findBackendAnimIndexBySubstring;
    using game::runtime::session_backend_render_helpers::makeBackendCardPrewarmLabel;
    using game::runtime::session_backend_render_helpers::prewarmBackendWorldTexturesForMesh;
    using game::runtime::session_backend_render_helpers::resolveBackendAnimIndexByName;

    {
        if (makeBackendCardPrewarmLabel("assets/ui/fire_tail_card.png") != "Fire tail card" ||
            makeBackendCardPrewarmLabel("") != "Card") {
            outFail = "SessionBackendRenderHelpers should derive readable card labels from texture paths.";
            return false;
        }
    }

    {
        std::vector<engine::render::model_types::AnimationClip> animations(4);
        animations[0].name = "battlewait";
        animations[1].name = "attack01";
        animations[2].name = "Down01_START";
        animations[3].name = "fly_move";

        if (resolveBackendAnimIndexByName(animations, "attack01.gfbanm") != 1 ||
            resolveBackendAnimIndexByName(animations, "down01_start") != 2 ||
            findBackendAnimIndexBySubstring(animations, {"move", "run"}) != 3) {
            outFail = "SessionBackendRenderHelpers should resolve backend animation roles across suffix and substring fallbacks.";
            return false;
        }
    }

    {
        RecordingBackend backend;
        game::runtime::render_model::MeshData mesh;
        mesh.submeshBaseTextures.push_back(makeTexture(2, 2));
        mesh.submeshBaseTextures.push_back({});
        mesh.submeshNormalTextures.push_back(makeTexture(2, 2));
        mesh.submeshEmissiveTextures.push_back({});
        mesh.submeshEmissiveTextures.push_back(makeTexture(1, 1));

        const std::size_t warmed = prewarmBackendWorldTexturesForMesh(&backend, &mesh);
        if (warmed != 2u || backend.calls.size() != 2u) {
            outFail = "SessionBackendRenderHelpers should prewarm one backend texture payload per textured submesh.";
            return false;
        }

        if (backend.calls[0].key.find("#submesh:0") == std::string::npos ||
            backend.calls[0].normalKey.find("#submesh_normal:0") == std::string::npos ||
            backend.calls[0].cacheKey.find("|srgb") == std::string::npos ||
            backend.calls[1].emissiveKey.find("#submesh_emissive:1") == std::string::npos) {
            outFail = "SessionBackendRenderHelpers should preserve generated cache keys for base, normal, and emissive prewarm textures.";
            return false;
        }
    }

    return true;
}
