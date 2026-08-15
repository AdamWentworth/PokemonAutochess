#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"

#include "game/runtime/video/VideoPreferences.h"

namespace game::runtime::shared_projected_unit_backend_mesh_support {

namespace {

bool usesNativePackedMaterialParameters(std::uint8_t materialMode) {
    return materialMode ==
               game::runtime::render_model::kNativeLayeredUnlitMaterialMode ||
           materialMode ==
               game::runtime::render_model::kNativeEyeClearCoatMaterialMode ||
           materialMode == game::runtime::render_model::
                               kNativeAnimatedEyeMaterialMode ||
           materialMode == game::runtime::render_model::
                               kNativeAnimatedEyeClearCoatMaterialMode ||
           materialMode == game::runtime::render_model::
                               kNativeFresnelEffectMaterialMode;
}

bool usesNativeTextureDetailLodBias(std::uint8_t materialMode) {
    return materialMode ==
               game::runtime::render_model::kNativeEyeClearCoatMaterialMode ||
           materialMode == game::runtime::render_model::
                               kNativeAnimatedEyeClearCoatMaterialMode ||
           materialMode == game::runtime::render_model::
                               kNativeFresnelEffectMaterialMode;
}

} // namespace

float textureDetailLodBiasForGraphicsQuality(int graphicsQuality) {
    switch (static_cast<game::video::GraphicsQuality>(
        game::video::sanitizeGraphicsQuality(graphicsQuality))) {
    case game::video::GraphicsQuality::Low:
        return 0.90f;
    case game::video::GraphicsQuality::Medium:
        return 0.45f;
    case game::video::GraphicsQuality::High:
        return 0.00f;
    case game::video::GraphicsQuality::Ultra:
    default:
        return -0.40f;
    }
}

void applyGraphicsQualityToBatchTemplate(
    game::runtime::shared_world_batches::WorldIndexedBatch& batch,
    int graphicsQuality) {
    // Native Game Freak materials reuse legacy texture slots and most flipbook
    // lanes for source shader parameters. EyeClearCoat's exact roughness now
    // lives in materialRect0U, leaving materialFlipbook1Frames available for
    // the inspector's ordinary texture-detail LOD bias. All native source maps
    // remain present at every quality tier.
    if (usesNativePackedMaterialParameters(batch.materialMode)) {
        if (usesNativeTextureDetailLodBias(batch.materialMode)) {
            batch.materialFlipbook1Frames =
                textureDetailLodBiasForGraphicsQuality(graphicsQuality);
        }
        return;
    }

    const int sanitizedQuality = game::video::sanitizeGraphicsQuality(graphicsQuality);
    batch.materialFlipbook1Frames = textureDetailLodBiasForGraphicsQuality(sanitizedQuality);
    if (sanitizedQuality >= static_cast<int>(game::video::GraphicsQuality::Ultra)) {
        return;
    }

    const bool nativeIkCharacter = batch.materialMode ==
        game::runtime::render_model::kNativeIkCharacterMaterialMode;
    if (!nativeIkCharacter) {
        batch.occlusionTextureKey.clear();
        batch.occlusionTextureCacheKey.clear();
        batch.occlusionTextureRgba = nullptr;
        batch.occlusionTextureWidth = 0;
        batch.occlusionTextureHeight = 0;
        batch.occlusionStrength = 1.0f;
    }

    batch.emissiveTextureKey.clear();
    batch.emissiveTextureCacheKey.clear();
    batch.emissiveTextureRgba = nullptr;
    batch.emissiveTextureWidth = 0;
    batch.emissiveTextureHeight = 0;
    batch.emissiveFactorR = 0.0f;
    batch.emissiveFactorG = 0.0f;
    batch.emissiveFactorB = 0.0f;

    if (sanitizedQuality >= static_cast<int>(game::video::GraphicsQuality::High)) {
        return;
    }

    batch.normalTextureKey.clear();
    batch.normalTextureCacheKey.clear();
    batch.normalTextureRgba = nullptr;
    batch.normalTextureWidth = 0;
    batch.normalTextureHeight = 0;
    batch.normalScale = 0.0f;

    if (sanitizedQuality >= static_cast<int>(game::video::GraphicsQuality::Medium)) {
        return;
    }

    // SV's SSS family samples its scalar roughness atlas in every proven
    // selected program. Low keeps that foundational map with coarser filtering;
    // Ultra restores its full detail. Only an explicit surface qualifier may
    // opt into Phlosion's additional fibre/velvet reconstruction.
    if (batch.materialMode ==
            game::runtime::render_model::kNativeSssMaterialMode) {
        return;
    }

    // Native Z-A IkCharacter materials repurpose this texture as baked shadow
    // colors plus their authored per-layer specular mask. It is foundational
    // material data, not an optional metallic/roughness detail map, so Low may
    // discard normal/AO/rim response but must retain this payload.
    if (batch.materialMode ==
            game::runtime::render_model::kNativeIkCharacterMaterialMode ||
        (batch.materialMode ==
            game::runtime::render_model::kNativeFacialOverlayMaterialMode &&
         batch.materialFlags > 3.5f && batch.materialFlags < 4.5f)) {
        return;
    }

    batch.metallicRoughnessTextureKey.clear();
    batch.metallicRoughnessTextureCacheKey.clear();
    batch.metallicRoughnessTextureRgba = nullptr;
    batch.metallicRoughnessTextureWidth = 0;
    batch.metallicRoughnessTextureHeight = 0;
    batch.metallicFactor = 0.0f;
    batch.roughnessFactor = 1.0f;
}

void applyGraphicsQualityToWorldSceneMaterial(
    IRenderBackend::WorldSceneMaterial& material,
    int graphicsQuality) {
    if (usesNativePackedMaterialParameters(material.materialMode)) {
        if (usesNativeTextureDetailLodBias(material.materialMode)) {
            material.materialFlipbook1Frames =
                textureDetailLodBiasForGraphicsQuality(graphicsQuality);
        }
        return;
    }

    const int sanitizedQuality = game::video::sanitizeGraphicsQuality(graphicsQuality);
    material.materialFlipbook1Frames = textureDetailLodBiasForGraphicsQuality(sanitizedQuality);
    if (sanitizedQuality >= static_cast<int>(game::video::GraphicsQuality::Ultra)) {
        return;
    }

    const bool nativeIkCharacter = material.materialMode ==
        game::runtime::render_model::kNativeIkCharacterMaterialMode;
    if (!nativeIkCharacter) {
        material.occlusionTextureKey.clear();
        material.occlusionTextureCacheKey.clear();
        material.occlusionTextureRgba = nullptr;
        material.occlusionTextureWidth = 0;
        material.occlusionTextureHeight = 0;
        material.occlusionStrength = 1.0f;
    }

    material.emissiveTextureKey.clear();
    material.emissiveTextureCacheKey.clear();
    material.emissiveTextureRgba = nullptr;
    material.emissiveTextureWidth = 0;
    material.emissiveTextureHeight = 0;
    material.emissiveFactorR = 0.0f;
    material.emissiveFactorG = 0.0f;
    material.emissiveFactorB = 0.0f;

    if (sanitizedQuality >= static_cast<int>(game::video::GraphicsQuality::High)) {
        return;
    }

    material.normalTextureKey.clear();
    material.normalTextureCacheKey.clear();
    material.normalTextureRgba = nullptr;
    material.normalTextureWidth = 0;
    material.normalTextureHeight = 0;
    material.normalScale = 0.0f;

    if (sanitizedQuality >= static_cast<int>(game::video::GraphicsQuality::Medium)) {
        return;
    }

    if (material.materialMode ==
            game::runtime::render_model::kNativeSssMaterialMode) {
        return;
    }

    if (material.materialMode ==
            game::runtime::render_model::kNativeIkCharacterMaterialMode ||
        (material.materialMode ==
            game::runtime::render_model::kNativeFacialOverlayMaterialMode &&
         material.materialFlags > 3.5f && material.materialFlags < 4.5f)) {
        return;
    }

    material.metallicRoughnessTextureKey.clear();
    material.metallicRoughnessTextureCacheKey.clear();
    material.metallicRoughnessTextureRgba = nullptr;
    material.metallicRoughnessTextureWidth = 0;
    material.metallicRoughnessTextureHeight = 0;
    material.metallicFactor = 0.0f;
    material.roughnessFactor = 1.0f;
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_support

