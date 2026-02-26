#pragma once

#include "game/runtime/SharedProjectedUnitModelRenderer.h"

namespace game::runtime::shared_projected_unit_backend_mesh {

using Args = shared_projected_unit_models::Args;
using Result = shared_projected_unit_models::Result;

Result renderProjectedUnitBackendMesh(const Args& args);

} // namespace game::runtime::shared_projected_unit_backend_mesh
