#pragma once

#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.h"

#include <glm/glm.hpp>

namespace game::runtime::shared_projected_unit_backend_mesh_triangle_loop {

struct Args {
    const shared_projected_unit_backend_mesh::Args* renderArgs = nullptr;
    shared_projected_unit_backend_mesh_prep::PreparedState* prep = nullptr;
    shared_projected_unit_backend_mesh_transforms::Resolver* transforms = nullptr;

    bool strictGltfParity = false;
    bool enableGpuClipSkinning = false;
    float captureVisualTintStrength = 0.0f;
    glm::vec3 captureTintColor{1.0f};
    float modelFadeAlpha = 1.0f;
    glm::vec3 cameraWorldPos{0.0f};
};

void appendFallbackTriangles(const Args& args);

} // namespace game::runtime::shared_projected_unit_backend_mesh_triangle_loop
