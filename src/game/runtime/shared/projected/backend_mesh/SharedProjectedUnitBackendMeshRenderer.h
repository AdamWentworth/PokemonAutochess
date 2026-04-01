#pragma once

#include "game/runtime/shared/projected/unit/SharedProjectedUnitModelRenderer.h"

class IRenderBackend;

namespace game::runtime::render_model {
struct MeshData;
}

namespace game::runtime::shared_projected_unit_backend_mesh {

using Args = shared_projected_unit_models::Args;
using Result = shared_projected_unit_models::Result;

Result renderProjectedUnitBackendMesh(const Args& args);
std::size_t prewarmProjectedUnitBackendMeshGeometryCache(
    IRenderBackend& renderer,
    const runtime::render_model::MeshData& mesh);

} // namespace game::runtime::shared_projected_unit_backend_mesh

