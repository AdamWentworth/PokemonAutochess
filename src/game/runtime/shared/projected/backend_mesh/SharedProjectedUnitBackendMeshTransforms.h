#pragma once
#include "engine/models/ModelTransforms.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshPrep.h"
namespace game::runtime::shared_projected_unit_backend_mesh_transforms {
using engine::models::transforms::ModelVertexSurfaceSample;
using engine::models::transforms::WorldVertexSample;
class Resolver : public engine::models::transforms::Resolver {
  public:
    void initialize(const shared_projected_unit_backend_mesh::Args &args,
                    const shared_projected_unit_backend_mesh_prep::PreparedState &prep);
};
} // namespace game::runtime::shared_projected_unit_backend_mesh_transforms
