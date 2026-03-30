#include "vfx/preview/growl/GrowlSharedRenderer.h"

#include <iostream>

#include <glm/gtc/type_ptr.hpp>

#include "engine/render/Camera3D.h"
#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/shared/backend/SharedBackendTextureCache.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"
#include "vfx/runtime/growl/SharedGrowlVfxHelpers.h"
#include "vfx/runtime/growl/SharedGrowlWaveBridge.h"
#include "vfx/runtime/growl/SharedGrowlWaveBatches.h"

namespace vfx::preview::growl {

void GrowlSharedRenderer::onResize(int width, int height) {
    backend_.onResize(width, height);
}

void GrowlSharedRenderer::render(const GrowlWaveVFX& effect,
                                 const Camera3D& camera,
                                 int surfaceWidth,
                                 int surfaceHeight) {
    GrowlWaveVFX::RenderSnapshot snapshot;
    if (!effect.buildRenderSnapshot(snapshot)) return;

    std::vector<game::runtime::shared_world_batches::WorldIndexedBatch> batches;
    batches.reserve(snapshot.drawPasses.size() * 4u);

    const auto resolveMesh =
        [&](const std::string& modelPath) -> game::runtime::render_model::MeshData* {
            return ensureBackendMeshLoaded(modelPath);
        };

    const auto resolveTexture =
        [&](const GrowlWaveVFX::Config::DrawPass& pass,
            const vfx::runtime::growl::TevState& tev,
            vfx::runtime::growl_batches::TextureView& outView) -> bool {
            return fillTextureView(pass, snapshot.config, tev, outView);
        };

    if (!vfx::runtime::growl_bridge::appendBatches(
            snapshot,
            batches,
            camera.getPosition(),
            resolveMesh,
            resolveTexture)) {
        return;
    }

    const glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    const glm::vec3 cameraPos = camera.getPosition();
    const glm::vec3 cameraForward = camera.getDirection();
    const glm::vec3 cameraTarget = camera.getTarget();
    game::runtime::shared_world_batches::submitWorldIndexedBatches(
        backend_,
        batches,
        glm::value_ptr(viewProj),
        surfaceWidth,
        surfaceHeight,
        glm::value_ptr(cameraPos),
        glm::value_ptr(cameraForward),
        glm::value_ptr(cameraTarget));
}

game::runtime::render_model::MeshData* GrowlSharedRenderer::ensureBackendMeshLoaded(
    const std::string& modelPath) {
    auto& cacheEntry = backendMeshByModelPath_[modelPath];
    if (!cacheEntry.attemptedLoad) {
        cacheEntry.attemptedLoad = true;
        std::string err;
        if (!game::runtime::render_model::loadMeshFromCache(modelPath, cacheEntry.mesh, &err)) {
            cacheEntry.error = std::move(err);
            cacheEntry.mesh = {};
        }
    }

    if (!cacheEntry.error.empty()) {
        if (!cacheEntry.reportedFailure) {
            std::cout << "[VfxLab] Unable to load cached mesh '" << modelPath
                      << "' (" << cacheEntry.error << ")\n";
            cacheEntry.reportedFailure = true;
        }
        return nullptr;
    }
    if (cacheEntry.mesh.vertices.empty() || cacheEntry.mesh.indices.empty()) return nullptr;
    return &cacheEntry.mesh;
}

game::runtime::SharedBackendTextureCacheEntry* GrowlSharedRenderer::ensureBackendTextureLoaded(
    const std::string& texturePath,
    bool flipVertical) {
    return game::runtime::session_texture_cache::ensureTextureLoaded(
        backendTextureByPath_,
        texturePath,
        flipVertical);
}

bool GrowlSharedRenderer::fillTextureViewFromEntry(
    const game::runtime::SharedBackendTextureCacheEntry* texture,
    vfx::runtime::growl_batches::TextureView& outView) {
    if (!texture || !texture->valid || texture->rgba.empty() ||
        texture->width <= 0 || texture->height <= 0) {
        return false;
    }
    outView.rgba = texture->rgba.data();
    outView.width = texture->width;
    outView.height = texture->height;
    return true;
}

bool GrowlSharedRenderer::fillTextureView(const GrowlWaveVFX::Config::DrawPass& pass,
                                          const GrowlWaveVFX::Config& config,
                                          const vfx::runtime::growl::TevState& tev,
                                          vfx::runtime::growl_batches::TextureView& outView) {
    if (vfx::runtime::growl::isLinePass(config, pass) || pass.texturePath.empty()) {
        return fillTextureViewFromEntry(ensureBackendTextureLoaded("", false), outView);
    }

    game::runtime::SharedBackendTextureCacheEntry* rawTexture =
        ensureBackendTextureLoaded(pass.texturePath, false);
    if (!rawTexture || !rawTexture->valid || rawTexture->rgba.empty()) return false;

    const bool quarterPass =
        vfx::runtime::growl::isQuarterRingPass(config, pass);
    const std::string bakedKey =
        vfx::runtime::growl::makeBakedTextureKey(pass, quarterPass);
    auto& baked = backendTextureByPath_[bakedKey];
    if (!baked.attemptedLoad) {
        baked.attemptedLoad = true;
        baked.valid = false;
        baked.width = rawTexture->width;
        baked.height = rawTexture->height;
        baked.rgba.clear();
        if (!vfx::runtime::growl::bakePassTextureRgba(
                pass,
                tev,
                quarterPass,
                rawTexture->rgba,
                baked.rgba)) {
            return false;
        }
        baked.valid = true;
    }

    return fillTextureViewFromEntry(&baked, outView);
}

} // namespace vfx::preview::growl
