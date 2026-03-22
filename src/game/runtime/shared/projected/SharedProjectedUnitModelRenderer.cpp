#include "game/runtime/shared/projected/SharedProjectedUnitModelRenderer.h"

#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.h"
#include "game/runtime/shared/projected/SharedProjectedUnitWorldSceneRenderer.h"

namespace game::runtime::shared_projected_unit_models {

Result renderProjectedUnitModel(const Args& args) {
    Result out{};
    if (!args.dataDb || !args.unit || !args.pose || !args.meshForUnit || !args.scenePose ||
        !args.tint || !args.projectedDebug || !args.sharedTailFireAnchors ||
        !args.worldIndexedBatches || !args.backendTextureByPath ||
        !args.modelDepthTris || !args.modelDepthWorldTris ||
        !args.remainingModelTrianglesBudget || !args.world3DTriangles ||
        !args.ensureBackendTextureLoaded ||
        !args.backendModelTriangleLimit || !args.backendModelFullMeshEnabled ||
        !args.backendModelFastTexturedPathEnabled || !args.backendModelBackfaceCullingEnabled) {
        return out;
    }
    if (shared_projected_unit_world_scene::tryRenderProjectedUnitModelWorldScene(args, out)) {
        return out;
    }
    return shared_projected_unit_backend_mesh::renderProjectedUnitBackendMesh(args);
}

} // namespace game::runtime::shared_projected_unit_models
