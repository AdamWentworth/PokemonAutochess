#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "engine/render/OpenGLRenderBackend.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/session/SessionTextureCache.h"
#include "vfx/effects/growl/GrowlWaveVFX.h"
#include "vfx/runtime/growl/SharedGrowlVfxHelpers.h"
#include "vfx/runtime/growl/SharedGrowlWaveBatches.h"

namespace vfx::preview::growl {

class GrowlSharedRenderer {
public:
    void onResize(int width, int height);

    void render(const GrowlWaveVFX& effect,
                const Camera3D& camera,
                int surfaceWidth,
                int surfaceHeight);

private:
    struct BackendMeshCacheEntry {
        bool attemptedLoad = false;
        bool reportedFailure = false;
        game::runtime::render_model::MeshData mesh;
        std::string error;
    };

    game::runtime::render_model::MeshData* ensureBackendMeshLoaded(const std::string& modelPath);
    game::runtime::SharedBackendTextureCacheEntry* ensureBackendTextureLoaded(
        const std::string& texturePath,
        bool flipVertical = false);
    static bool fillTextureViewFromEntry(
        const game::runtime::SharedBackendTextureCacheEntry* texture,
        vfx::runtime::growl_batches::TextureView& outView);
    bool fillTextureView(const GrowlWaveVFX::Config::DrawPass& pass,
                         const GrowlWaveVFX::Config& config,
                         const vfx::runtime::growl::TevState& tev,
                         vfx::runtime::growl_batches::TextureView& outView);

    OpenGLRenderBackend backend_;
    std::unordered_map<std::string, BackendMeshCacheEntry> backendMeshByModelPath_;
    game::runtime::session_texture_cache::TextureCache backendTextureByPath_;
};

} // namespace vfx::preview::growl
