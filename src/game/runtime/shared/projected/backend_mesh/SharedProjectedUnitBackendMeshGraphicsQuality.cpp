#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"

#include "game/runtime/video/VideoPreferences.h"

namespace game::runtime::shared_projected_unit_backend_mesh_support {

namespace {

bool usesNativePackedMaterialParameters(std::uint8_t materialMode) {
    return materialMode ==
               game::runtime::render_model::kNativeLayeredUnlitMaterialMode ||
           materialMode ==
               game::runtime::render_model::kNativeEyeClearCoatMaterialMode;
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
    const int sanitizedQuality = game::video::sanitizeGraphicsQuality(graphicsQuality);
    batch.textureDetailLodBias =
        textureDetailLodBiasForGraphicsQuality(sanitizedQuality);

    // Native Game Freak modes assign source-specific meanings to these slots:
    // normal can be displacement, metallic/roughness can be a layer mask, and
    // the legacy flipbook values carry material constants. They still receive
    // the quality LOD bias through the dedicated internal field, but their
    // source maps must remain intact at every tier.
    if (usesNativePackedMaterialParameters(batch.materialMode)) return;

    // Restore the established quality budget. Mip bias controls base texture
    // detail on all three APIs, while progressively dropping secondary maps
    // provides the larger memory/bandwidth and shader-response separation that
    // originally distinguished the four tiers.
    if (sanitizedQuality >= static_cast<int>(game::video::GraphicsQuality::Ultra)) {
        return;
    }

    batch.occlusionTextureKey.clear();
    batch.occlusionTextureCacheKey.clear();
    batch.ownedOcclusionTextureRgba.clear();
    batch.occlusionTextureRgba = nullptr;
    batch.occlusionTextureWidth = 0;
    batch.occlusionTextureHeight = 0;
    batch.occlusionTextureMipLevels = nullptr;
    batch.occlusionTextureMipLevelCount = 0u;
    batch.occlusionStrength = 1.0f;

    batch.emissiveTextureKey.clear();
    batch.emissiveTextureCacheKey.clear();
    batch.ownedEmissiveTextureRgba.clear();
    batch.emissiveTextureRgba = nullptr;
    batch.emissiveTextureWidth = 0;
    batch.emissiveTextureHeight = 0;
    batch.emissiveTextureMipLevels = nullptr;
    batch.emissiveTextureMipLevelCount = 0u;
    batch.emissiveFactorR = 0.0f;
    batch.emissiveFactorG = 0.0f;
    batch.emissiveFactorB = 0.0f;

    if (sanitizedQuality >= static_cast<int>(game::video::GraphicsQuality::High)) {
        return;
    }

    batch.normalTextureKey.clear();
    batch.normalTextureCacheKey.clear();
    batch.ownedNormalTextureRgba.clear();
    batch.normalTextureRgba = nullptr;
    batch.normalTextureWidth = 0;
    batch.normalTextureHeight = 0;
    batch.normalTextureMipLevels = nullptr;
    batch.normalTextureMipLevelCount = 0u;
    batch.normalScale = 0.0f;

    if (sanitizedQuality >= static_cast<int>(game::video::GraphicsQuality::Medium)) {
        return;
    }

    batch.metallicRoughnessTextureKey.clear();
    batch.metallicRoughnessTextureCacheKey.clear();
    batch.ownedMetallicRoughnessTextureRgba.clear();
    batch.metallicRoughnessTextureRgba = nullptr;
    batch.metallicRoughnessTextureWidth = 0;
    batch.metallicRoughnessTextureHeight = 0;
    batch.metallicRoughnessTextureMipLevels = nullptr;
    batch.metallicRoughnessTextureMipLevelCount = 0u;
    batch.metallicFactor = 0.0f;
    batch.roughnessFactor = 1.0f;
}

void applyGraphicsQualityToWorldSceneMaterial(
    IRenderBackend::WorldSceneMaterial& material,
    int graphicsQuality) {
    const int sanitizedQuality = game::video::sanitizeGraphicsQuality(graphicsQuality);
    // Modes 2/27/28 do not use projected-shadow bias. Reuse that established
    // public ABI slot instead of extending WorldSceneMaterial across the
    // editor-plugin DLL boundary.
    material.projectedShadowBias =
        textureDetailLodBiasForGraphicsQuality(sanitizedQuality);

    if (usesNativePackedMaterialParameters(material.materialMode)) return;
    if (sanitizedQuality >= static_cast<int>(game::video::GraphicsQuality::Ultra)) {
        return;
    }

    material.occlusionTextureKey.clear();
    material.occlusionTextureCacheKey.clear();
    material.occlusionTextureRgba = nullptr;
    material.occlusionTextureWidth = 0;
    material.occlusionTextureHeight = 0;
    material.occlusionTextureMipLevels = nullptr;
    material.occlusionTextureMipLevelCount = 0u;
    material.occlusionStrength = 1.0f;

    material.emissiveTextureKey.clear();
    material.emissiveTextureCacheKey.clear();
    material.emissiveTextureRgba = nullptr;
    material.emissiveTextureWidth = 0;
    material.emissiveTextureHeight = 0;
    material.emissiveTextureMipLevels = nullptr;
    material.emissiveTextureMipLevelCount = 0u;
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
    material.normalTextureMipLevels = nullptr;
    material.normalTextureMipLevelCount = 0u;
    material.normalScale = 0.0f;

    if (sanitizedQuality >= static_cast<int>(game::video::GraphicsQuality::Medium)) {
        return;
    }

    material.metallicRoughnessTextureKey.clear();
    material.metallicRoughnessTextureCacheKey.clear();
    material.metallicRoughnessTextureRgba = nullptr;
    material.metallicRoughnessTextureWidth = 0;
    material.metallicRoughnessTextureHeight = 0;
    material.metallicRoughnessTextureMipLevels = nullptr;
    material.metallicRoughnessTextureMipLevelCount = 0u;
    material.metallicFactor = 0.0f;
    material.roughnessFactor = 1.0f;
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_support

