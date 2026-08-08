#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"

#include "game/runtime/video/VideoPreferences.h"

namespace game::runtime::shared_projected_unit_backend_mesh_support {

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
    // Graphics quality is a texture-resolution control. Keep the complete
    // authored material at every tier and select a different mip instead of
    // deleting maps. Dropping metallic/roughness at Low made Machoke's gold
    // belt hardware look unfinished, while dropping normal/emissive maps
    // changed a Pokemon's material identity rather than its texture quality.
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
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_support

