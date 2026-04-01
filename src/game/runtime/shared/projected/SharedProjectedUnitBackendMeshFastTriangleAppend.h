#pragma once

#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace game::runtime::shared_projected_unit_backend_mesh_fast_triangle_append {

struct Args {
    const runtime::render_model::MeshData* mesh = nullptr;
    shared_projected_unit_backend_mesh_transforms::Resolver* transforms = nullptr;
    shared_world_batches::WorldIndexedBatch* fastBatch = nullptr;
    std::vector<std::vector<int>>* modelIndexedVertexRemap = nullptr;
    const std::vector<std::uint8_t>* fastBatchUsesRigidNodeGpuSkin = nullptr;
    const std::vector<std::unordered_map<int, std::uint16_t>>* fastBatchRigidNodePaletteIndex =
        nullptr;
    std::vector<std::unordered_map<std::uint64_t, std::uint32_t>>*
        fastBatchRigidVertexRemap = nullptr;

    std::size_t fastBatchIndex = 0u;
    int triNodeIndex = -1;
    bool needsLitNormalsForSubmesh = false;
    bool needsTangentsForSubmesh = false;

    std::uint32_t i0 = 0u;
    std::uint32_t i1 = 0u;
    std::uint32_t i2 = 0u;
    const runtime::render_model::MeshVertex* v0 = nullptr;
    const runtime::render_model::MeshVertex* v1 = nullptr;
    const runtime::render_model::MeshVertex* v2 = nullptr;
};

bool appendFastTexturedTriangle(const Args& args);

} // namespace game::runtime::shared_projected_unit_backend_mesh_fast_triangle_append
