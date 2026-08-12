#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace {

constexpr int kClampToEdge = 33071;
constexpr unsigned char kFallbackWhiteRgba[4] = {255u, 255u, 255u, 255u};

std::string makeIndexedBatchKeyPrefix(
    const game::runtime::render_model::MeshData& mesh) {
    if (!mesh.assetCacheIdentity.empty()) {
        return "__runtime_mesh_id__:" +
               mesh.assetCacheIdentity;
    }
    return "__runtime_mesh__:" +
           std::to_string(static_cast<unsigned long long>(
               reinterpret_cast<std::uintptr_t>(&mesh)));
}

std::string buildWorldTextureCacheKey(const std::string& key,
                                      int width,
                                      int height,
                                      int wrapS,
                                      int wrapT,
                                      bool srgb) {
    if (key.empty() || width <= 0 || height <= 0) {
        return {};
    }
    std::string cacheKey = key;
    cacheKey += "|";
    cacheKey += std::to_string(width);
    cacheKey += "x";
    cacheKey += std::to_string(height);
    cacheKey += "|ws=";
    cacheKey += std::to_string(wrapS);
    cacheKey += "|wt=";
    cacheKey += std::to_string(wrapT);
    cacheKey += srgb ? "|srgb" : "|lin";
    return cacheKey;
}

auto& fastTexturedMaterialTemplateCaches() {
    using Cache = game::runtime::shared_projected_unit_backend_mesh_support::
        FastTexturedMaterialTemplateCache;
    using Variants = std::array<Cache, 8u>;
    static thread_local std::unordered_map<
        const game::runtime::render_model::MeshData*,
        Variants> caches;
    return caches;
}

} // namespace

namespace game::runtime::shared_projected_unit_backend_mesh_support {

const FastTexturedMaterialTemplateCache* ensureFastTexturedMaterialTemplateCache(
    const game::runtime::render_model::MeshData* mesh,
    std::size_t baseBatchCount,
    bool characterInkingEnabled,
    int graphicsQuality) {
    if (!mesh || baseBatchCount == 0u) {
        return nullptr;
    }

    const int qualityVariant = std::clamp(graphicsQuality, 0, 3);
    const std::size_t materialVariant =
        static_cast<std::size_t>(qualityVariant) * 2u +
        (characterInkingEnabled ? 1u : 0u);
    auto& cache =
        fastTexturedMaterialTemplateCaches()[mesh][materialVariant];
    const bool cacheValid =
        cache.mesh == mesh &&
        cache.assetCacheIdentitySnapshot ==
            mesh->assetCacheIdentity &&
        cache.meshVertexCount == mesh->vertices.size() &&
        cache.meshIndexCount == mesh->indices.size() &&
        cache.baseBatchCount == baseBatchCount &&
        cache.characterInkingEnabled == characterInkingEnabled &&
        cache.graphicsQuality == graphicsQuality &&
        cache.materials.size() == baseBatchCount;
    if (cacheValid) {
        return &cache;
    }

    cache = {};
    cache.mesh = mesh;
    cache.assetCacheIdentitySnapshot =
        mesh->assetCacheIdentity;
    cache.meshVertexCount = mesh->vertices.size();
    cache.meshIndexCount = mesh->indices.size();
    cache.baseBatchCount = baseBatchCount;
    cache.characterInkingEnabled = characterInkingEnabled;
    cache.graphicsQuality = graphicsQuality;
    cache.materials.resize(baseBatchCount);
    const std::string keyPrefix = makeIndexedBatchKeyPrefix(*mesh);

    for (std::size_t si = 0; si < baseBatchCount; ++si) {
        auto& material = cache.materials[si];
        if (si < mesh->submeshBaseTextures.size()) {
            const auto& tex = mesh->submeshBaseTextures[si];
            if (tex.hasPixels()) {
                material.textureKey = keyPrefix + "#submesh:" + std::to_string(si);
                material.textureCacheKey = buildWorldTextureCacheKey(
                    material.textureKey,
                    tex.width,
                    tex.height,
                    tex.wrapS,
                    tex.wrapT,
                    true);
                material.textureRgba = tex.rgba.data();
                material.textureWidth = tex.width;
                material.textureHeight = tex.height;
                material.textureWrapS = tex.wrapS;
                material.textureWrapT = tex.wrapT;
            }
        }
        if (!material.textureRgba || material.textureWidth <= 0 || material.textureHeight <= 0) {
            material.textureKey = "__fallback_white_1x1__";
            material.textureCacheKey = buildWorldTextureCacheKey(
                material.textureKey,
                1,
                1,
                kClampToEdge,
                kClampToEdge,
                true);
            material.textureRgba = kFallbackWhiteRgba;
            material.textureWidth = 1;
            material.textureHeight = 1;
            material.textureWrapS = kClampToEdge;
            material.textureWrapT = kClampToEdge;
        }

        if (si < mesh->submeshNormalTextures.size()) {
            const auto& normalTex = mesh->submeshNormalTextures[si];
            if (normalTex.hasPixels()) {
                material.normalTextureKey =
                    keyPrefix + "#submesh_normal:" + std::to_string(si);
                material.normalTextureCacheKey = buildWorldTextureCacheKey(
                    material.normalTextureKey,
                    normalTex.width,
                    normalTex.height,
                    normalTex.wrapS,
                    normalTex.wrapT,
                    false);
                material.normalTextureRgba = normalTex.rgba.data();
                material.normalTextureWidth = normalTex.width;
                material.normalTextureHeight = normalTex.height;
                material.normalTextureWrapS = normalTex.wrapS;
                material.normalTextureWrapT = normalTex.wrapT;
            }
        }
        if (si < mesh->submeshMetallicRoughnessTextures.size()) {
            const auto& metallicRoughnessTex = mesh->submeshMetallicRoughnessTextures[si];
            if (metallicRoughnessTex.hasPixels()) {
                material.metallicRoughnessTextureKey =
                    keyPrefix + "#submesh_mr:" + std::to_string(si);
                material.metallicRoughnessTextureCacheKey = buildWorldTextureCacheKey(
                    material.metallicRoughnessTextureKey,
                    metallicRoughnessTex.width,
                    metallicRoughnessTex.height,
                    metallicRoughnessTex.wrapS,
                    metallicRoughnessTex.wrapT,
                    false);
                material.metallicRoughnessTextureRgba = metallicRoughnessTex.rgba.data();
                material.metallicRoughnessTextureWidth = metallicRoughnessTex.width;
                material.metallicRoughnessTextureHeight = metallicRoughnessTex.height;
                material.metallicRoughnessTextureWrapS = metallicRoughnessTex.wrapS;
                material.metallicRoughnessTextureWrapT = metallicRoughnessTex.wrapT;
            }
        }
        if (si < mesh->submeshOcclusionTextures.size()) {
            const auto& occlusionTex = mesh->submeshOcclusionTextures[si];
            if (occlusionTex.hasPixels()) {
                material.occlusionTextureKey =
                    keyPrefix + "#submesh_occ:" + std::to_string(si);
                material.occlusionTextureCacheKey = buildWorldTextureCacheKey(
                    material.occlusionTextureKey,
                    occlusionTex.width,
                    occlusionTex.height,
                    occlusionTex.wrapS,
                    occlusionTex.wrapT,
                    false);
                material.occlusionTextureRgba = occlusionTex.rgba.data();
                material.occlusionTextureWidth = occlusionTex.width;
                material.occlusionTextureHeight = occlusionTex.height;
                material.occlusionTextureWrapS = occlusionTex.wrapS;
                material.occlusionTextureWrapT = occlusionTex.wrapT;
            }
        }
        if (si < mesh->submeshEmissiveTextures.size()) {
            const auto& emissiveTex = mesh->submeshEmissiveTextures[si];
            if (emissiveTex.hasPixels()) {
                material.emissiveTextureKey =
                    keyPrefix + "#submesh_emissive:" + std::to_string(si);
                material.emissiveTextureCacheKey = buildWorldTextureCacheKey(
                    material.emissiveTextureKey,
                    emissiveTex.width,
                    emissiveTex.height,
                    emissiveTex.wrapS,
                    emissiveTex.wrapT,
                    true);
                material.emissiveTextureRgba = emissiveTex.rgba.data();
                material.emissiveTextureWidth = emissiveTex.width;
                material.emissiveTextureHeight = emissiveTex.height;
                material.emissiveTextureWrapS = emissiveTex.wrapS;
                material.emissiveTextureWrapT = emissiveTex.wrapT;
            }
        }

        if (si < mesh->submeshAlphaMode.size()) {
            material.alphaMode = mesh->submeshAlphaMode[si];
        }
        if (si < mesh->submeshAlphaCutoff.size()) {
            material.alphaCutoff = mesh->submeshAlphaCutoff[si];
        }
        if (si < mesh->submeshNormalScale.size()) {
            material.normalScale = std::max(0.0f, mesh->submeshNormalScale[si]);
        }
        if (si < mesh->submeshMetallicFactor.size()) {
            material.metallicFactor =
                std::clamp(mesh->submeshMetallicFactor[si], 0.0f, 1.0f);
        }
        if (si < mesh->submeshRoughnessFactor.size()) {
            material.roughnessFactor =
                std::clamp(mesh->submeshRoughnessFactor[si], 0.0f, 1.0f);
        }
        if (si < mesh->submeshOcclusionStrength.size()) {
            material.occlusionStrength =
                std::clamp(mesh->submeshOcclusionStrength[si], 0.0f, 1.0f);
        }
        if (si < mesh->submeshEmissiveFactors.size()) {
            const glm::vec3& emissive = mesh->submeshEmissiveFactors[si];
            material.emissiveFactorR = std::max(0.0f, emissive.r);
            material.emissiveFactorG = std::max(0.0f, emissive.g);
            material.emissiveFactorB = std::max(0.0f, emissive.b);
        }
        material.materialMode =
            si < mesh->submeshMaterialModes.size()
                ? mesh->submeshMaterialModes[si]
                : 2u;
        material.materialFlags =
            si < mesh->submeshMaterialFlags.size()
                ? mesh->submeshMaterialFlags[si]
                : 0.0f;
        if (si < mesh->submeshMaterialParams0.size()) {
            const glm::vec4& value = mesh->submeshMaterialParams0[si];
            if (material.materialMode == game::runtime::render_model::
                                             kNativeFacialOverlayMaterialMode) {
                material.clipSpaceDepthBias = std::max(0.0f, value.x);
            }
            material.materialRect0U = value.x;
            material.materialRect0V = value.y;
            material.materialRect0W = value.z;
            material.materialRect0H = value.w;
            if (material.materialMode == game::runtime::render_model::
                                             kNativeEyeClearCoatMaterialMode ||
                material.materialMode == game::runtime::render_model::
                                             kNativeAnimatedEyeClearCoatMaterialMode) {
                // This slot is outside the generic PBR camera packing and is
                // unused by ordinary model shading. Preserve the source
                // clear-coat roughness for the dedicated eye program.
                material.materialFlipbook1Frames = value.x;
            }
        }
        if (si < mesh->submeshMaterialParams1.size()) {
            const glm::vec4& value = mesh->submeshMaterialParams1[si];
            material.materialRect1U = value.x;
            material.materialRect1V = value.y;
            material.materialRect1W = value.z;
            material.materialRect1H = value.w;
        }
        if (material.materialMode == game::runtime::render_model::
                                         kNativeLayeredUnlitMaterialMode) {
            if (si < mesh->submeshMaterialParams2.size()) {
                const glm::vec4& value = mesh->submeshMaterialParams2[si];
                material.materialFlipbook0Cols = value.x;
                material.materialFlipbook0Rows = value.y;
                material.materialFlipbook0Frames = value.z;
                material.materialFlipbook0Fps = value.w;
            }
            if (si < mesh->submeshMaterialParams3.size()) {
                const glm::vec4& value = mesh->submeshMaterialParams3[si];
                material.materialFlipbook1Cols = value.x;
                material.materialFlipbook1Rows = value.y;
                material.materialFlipbook1Frames = value.z;
                material.materialFlipbook1Fps = value.w;
            }
        } else if (
            material.materialMode == game::runtime::render_model::
                                         kNativeAnimatedEyeMaterialMode ||
            material.materialMode == game::runtime::render_model::
                                         kNativeAnimatedEyeClearCoatMaterialMode) {
            if (si < mesh->submeshMaterialParams2.size()) {
                const glm::vec4& value =
                    mesh->submeshMaterialParams2[si];
                material.lightProjectionUvRowU = {
                    value.x,
                    value.y,
                    value.z,
                    value.w};
            }
        }
        if (material.materialMode == game::runtime::render_model::
                                         kNativeSssFurMaterialMode) {
            material.materialFlipbook1Frames =
                textureDetailLodBiasForGraphicsQuality(graphicsQuality);
        }
        material.characterInkingEnabled =
            material.materialMode == game::runtime::render_model::
                                         kNativeLayeredUnlitMaterialMode ||
                material.materialMode == game::runtime::render_model::
                                             kNativeEyeClearCoatMaterialMode ||
                material.materialMode == game::runtime::render_model::
                                             kNativeAnimatedEyeMaterialMode ||
                material.materialMode == game::runtime::render_model::
                                             kNativeAnimatedEyeClearCoatMaterialMode
                ? 0u
                : (characterInkingEnabled ? 1u : 0u);
        applyGraphicsQualityToWorldSceneMaterial(material, graphicsQuality);
    }

    return &cache;
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_support

