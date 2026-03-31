#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "engine/render/OpenGLRenderBackend.h"
#include "vfx/effects/growl/GrowlWaveVFX.h"
#include "vfx/runtime/growl/SharedGrowlVfxHelpers.h"
#include "vfx/runtime/growl/SharedGrowlWaveBatches.h"

class Camera3D;

namespace vfx::preview::growl {

namespace detail {

bool loadMeshForPreview(const std::string& modelPath,
                        vfx::runtime::growl_batches::MeshData& out,
                        std::string* outError = nullptr);

} // namespace detail

class GrowlSharedRenderer {
public:
    void onResize(int width, int height);

    void render(const GrowlWaveVFX& effect,
                const Camera3D& camera,
                int surfaceWidth,
                int surfaceHeight);

    struct BackendMeshCacheEntry {
        bool attemptedLoad = false;
        bool reportedFailure = false;
        vfx::runtime::growl_batches::MeshData mesh;
        std::string error;
    };

    struct TextureCacheEntry {
        bool attemptedLoad = false;
        bool valid = false;
        int width = 0;
        int height = 0;
        std::vector<unsigned char> rgba;
    };

private:
    vfx::runtime::growl_batches::MeshData* ensureBackendMeshLoaded(const std::string& modelPath);
    TextureCacheEntry* ensureBackendTextureLoaded(const std::string& texturePath,
                                                  bool flipVertical = false);
    static bool fillTextureViewFromEntry(const TextureCacheEntry* texture,
                                         vfx::runtime::growl_batches::TextureView& outView);
    bool fillTextureView(const GrowlWaveVFX::Config::DrawPass& pass,
                         const GrowlWaveVFX::Config& config,
                         const vfx::runtime::growl::TevState& tev,
                         vfx::runtime::growl_batches::TextureView& outView);

    OpenGLRenderBackend backend_;
    std::unordered_map<std::string, BackendMeshCacheEntry> backendMeshByModelPath_;
    std::unordered_map<std::string, TextureCacheEntry> backendTextureByPath_;
};

} // namespace vfx::preview::growl
