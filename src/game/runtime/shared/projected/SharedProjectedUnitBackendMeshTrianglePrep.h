#pragma once

#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshSupport.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_projected_unit_backend_mesh_triangle_prep {

struct State {
    std::vector<std::uint8_t> fastBatchUsesRigidNodeGpuSkin;
    std::vector<std::unordered_map<int, std::uint16_t>> fastBatchRigidNodePaletteIndex;
    std::vector<std::unordered_map<std::uint64_t, std::uint32_t>> fastBatchRigidVertexRemap;
};

struct Args {
    const runtime::render_model::MeshData* mesh = nullptr;
    const std::vector<int>* submeshNodeFallback = nullptr;
    const std::vector<glm::mat4>* nodeGlobals = nullptr;
    const glm::mat4* modelMatrix = nullptr;
    std::size_t triangleCount = 0u;
    bool useFastTexturedFullMeshPath = false;
    bool enableGpuClipSkinning = false;

    std::vector<int>* triNodeIndexByTriangle = nullptr;
    std::vector<shared_world_batches::WorldIndexedBatch>* modelIndexedBatchesPerSubmesh = nullptr;
};

void initializeIndexedTrianglePrep(const Args& args, State& state);

} // namespace game::runtime::shared_projected_unit_backend_mesh_triangle_prep
