#pragma once

#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshRenderer.h"

#include "game/runtime/shared/backend/SharedBackendPoseEval.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <array>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_projected_unit_backend_mesh_prep {

using Args = shared_projected_unit_backend_mesh::Args;
using Result = shared_projected_unit_backend_mesh::Result;

struct PreparedState {
    const runtime::render_model::MeshData* mesh = nullptr;
    std::size_t triangleCount = 0u;
    std::size_t effectiveUnitTriangleBudget = 0u;
    std::size_t modelIndexedBatchCount = 0u;

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
    std::array<float, 16> modelMatrix{};
    float indexedBatchSortDepth = 0.0f;

    std::size_t modelDepthCountBefore = 0u;
    std::size_t modelDepthWorldCountBefore = 0u;
    std::size_t world3DTriangleCountBefore = 0u;

    const runtime::shared_backend_pose::PoseEval* scenePose = nullptr;
    runtime::shared_backend_pose::PoseEval ownedScenePose{};

    const std::vector<int>* submeshNodeFallback = nullptr;
    std::vector<shared_world_batches::WorldIndexedBatch> modelIndexedBatchesPerSubmesh;
    std::vector<std::vector<int>> modelIndexedVertexRemap;

    void reset();
};

namespace detail {

// Samples one retained source material parameter. Exposed for parity tests;
// render code uses the same path before transporting values to each backend.
bool sampleContinuousMaterialAnimation(
    const runtime::render_model::MeshData& mesh,
    std::size_t submeshIndex,
    runtime::render_model::MaterialAnimationParameter parameter,
    float materialTimeSec,
    glm::vec4& outValue);

} // namespace detail

// Returns false when rendering should stop immediately (for example triangleCount==0 sets skipUnit).
bool prepareProjectedUnitBackendMesh(const Args& args, Result& out, PreparedState& prepared);
bool prepareProjectedUnitBackendMeshWorldScene(const Args& args,
                                               Result& out,
                                               PreparedState& prepared);

} // namespace game::runtime::shared_projected_unit_backend_mesh_prep

