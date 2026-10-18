#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"
#include "pokemon/models/MaterialCompatibility.h"
namespace game::runtime::shared_projected_unit_backend_mesh_support {
float textureDetailLodBiasForGraphicsQuality(int quality) {
    return pokemon::models::materials::textureDetailLodBiasForGraphicsQuality(quality);
}
void applyGraphicsQualityToBatchTemplate(shared_world_batches::WorldIndexedBatch &batch, int quality) {
    pokemon::models::materials::applyGraphicsQualityToBatchTemplate(batch, quality);
}
void applyGraphicsQualityToWorldSceneMaterial(IRenderBackend::WorldSceneMaterial &material, int quality) {
    pokemon::models::materials::applyGraphicsQualityToWorldSceneMaterial(material, quality);
}
} // namespace game::runtime::shared_projected_unit_backend_mesh_support
