#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "engine/render/OpenGLRenderBackend.h"
#include "vfx/effects/shared/SharedAuthoredBatchVFX.h"
#include "vfx/runtime/shared/SharedAuthoredVfxBatches.h"
#include "vfx/runtime/shared/SharedAuthoredVfxHelpers.h"

class Camera3D;

namespace vfx::preview::authored {

namespace detail {

bool loadMeshForPreview(const std::string& modelPath,
                        vfx::runtime::authored_batches::MeshData& out,
                        std::string* outError = nullptr);

} // namespace detail

class SharedAuthoredVfxRenderer {
public:
    void onResize(int width, int height);

    void render(const SharedAuthoredBatchVFX& effect,
                const Camera3D& camera,
                int surfaceWidth,
                int surfaceHeight);

    struct BackendMeshCacheEntry {
        bool attemptedLoad = false;
        bool reportedFailure = false;
        vfx::runtime::authored_batches::MeshData mesh;
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
    vfx::runtime::authored_batches::MeshData* ensureBackendMeshLoaded(const std::string& modelPath);
    TextureCacheEntry* ensureBackendTextureLoaded(const std::string& texturePath,
                                                  bool flipVertical = false);
    static bool fillTextureViewFromEntry(const TextureCacheEntry* texture,
                                         vfx::runtime::authored_batches::TextureView& outView);
    bool fillTextureView(const SharedAuthoredBatchVFX::Config::DrawPass& pass,
                         const SharedAuthoredBatchVFX::Config& config,
                         const vfx::runtime::authored::TevState& tev,
                         vfx::runtime::authored_batches::TextureView& outView);

    OpenGLRenderBackend backend_;
    std::unordered_map<std::string, BackendMeshCacheEntry> backendMeshByModelPath_;
    std::unordered_map<std::string, TextureCacheEntry> backendTextureByPath_;
};

} // namespace vfx::preview::authored
