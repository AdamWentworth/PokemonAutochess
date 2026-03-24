#include "game/runtime/shared/projected/SharedProjectedUnitModelRenderer.h"

#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.h"
#include "game/runtime/shared/projected/SharedProjectedUnitWorldSceneRenderer.h"
#include "engine/core/Environment.h"

#include <sstream>

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
    const bool traceEnvPresent =
        args.unit &&
        engine::env::get("PAC_TRACE_PROJECTED_WORLD_SCENE").has_value();
    const bool traceWorldScene =
        args.unit &&
        shared_projected_unit_world_scene::shouldTraceProjectedUnitWorldScene(*args.unit);
    if (shared_projected_unit_world_scene::tryRenderProjectedUnitModelWorldScene(args, out)) {
        if (traceEnvPresent) {
            std::ostringstream line;
            line
                << "[ProjectedTrace][ModelPath] unit=" << args.unit->name
                << " id=" << args.unit->id
                << " traced=" << (traceWorldScene ? 1 : 0)
                << " path=world_scene";
            shared_projected_unit_world_scene::appendProjectedUnitWorldSceneTraceLine(
                line.str());
        }
        return out;
    }
    if (traceEnvPresent) {
        std::ostringstream line;
        line
            << "[ProjectedTrace][ModelPath] unit=" << args.unit->name
            << " id=" << args.unit->id
            << " traced=" << (traceWorldScene ? 1 : 0)
            << " path=legacy_backend_mesh";
        shared_projected_unit_world_scene::appendProjectedUnitWorldSceneTraceLine(
            line.str());
    }
    return shared_projected_unit_backend_mesh::renderProjectedUnitBackendMesh(args);
}

} // namespace game::runtime::shared_projected_unit_models
