#pragma once

#include "game/runtime/SharedProjectedUnitBackendMeshRenderer.h"

#include "game/runtime/SharedBackendPoseEval.h"
#include "game/runtime/SharedWorldIndexedBatches.h"

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_projected_unit_backend_mesh_prep {

using Args = shared_projected_unit_backend_mesh::Args;
using Result = shared_projected_unit_backend_mesh::Result;

struct PreparedState {
    const runtime::backend_model::MeshData* mesh = nullptr;
    std::size_t triangleCount = 0u;
    std::size_t effectiveUnitTriangleBudget = 0u;

    bool useIndexedWorldModelPath = false;
    bool fullIndexedMeshPath = false;
    bool useFastTexturedFullMeshPath = false;
    bool usePositionOnlyVertexPath = false;
    bool downsampleModelTriangles = false;

    float resolvedScaleCorrection = 1.0f;
    float fastTexturedAlpha = 1.0f;
    glm::vec3 fastTexturedTint{1.0f};
    glm::vec3 lightDir{0.0f, 1.0f, 0.0f};
    glm::vec3 fallbackBase{1.0f};

    glm::mat4 modelM{1.0f};

    std::size_t modelDepthCountBefore = 0u;
    std::size_t modelDepthWorldCountBefore = 0u;
    std::size_t world3DTriangleCountBefore = 0u;

    runtime::shared_backend_pose::PoseEval scenePose{};

    std::vector<int> submeshNodeFallback;
    std::vector<shared_world_batches::WorldIndexedBatch> modelIndexedBatchesPerSubmesh;
    std::vector<std::vector<int>> modelIndexedVertexRemap;
};

// Returns false when rendering should stop immediately (for example triangleCount==0 sets skipUnit).
bool prepareProjectedUnitBackendMesh(const Args& args, Result& out, PreparedState& prepared);

} // namespace game::runtime::shared_projected_unit_backend_mesh_prep
