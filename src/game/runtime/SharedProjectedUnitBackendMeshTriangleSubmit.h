#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/runtime/SharedProjectedDebugVfx.h"
#include "game/runtime/SharedProjectedWorldSceneHelpers.h"
#include "game/runtime/SharedWorldIndexedBatches.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_projected_unit_backend_mesh_submit {

class TriangleSubmitter {
  public:
    struct Args {
        bool supportsWorldTriangles3D = false;
        bool useIndexedWorldModelPath = false;
        bool fullIndexedMeshPath = false;
        bool fastTexturedPathEnabled = false;
        bool backfaceCullingEnabled = false;

        glm::vec3 cameraWorldPos{0.0f};
        glm::vec3 lightDir{0.0f, 1.0f, 0.0f};

        shared_projected_debug::ProjectedDebugVfxBuilder* projectedDebug = nullptr;
        std::vector<shared_world_batches::WorldIndexedBatch>* modelIndexedBatchesPerSubmesh = nullptr;
        std::vector<std::vector<int>>* modelIndexedVertexRemap = nullptr;
        std::vector<shared_projected_scene::DepthTri>* modelDepthTris = nullptr;
        std::vector<IRenderBackend::WorldTriangle>* world3DTriangles = nullptr;
    };

    void initialize(const Args& args);

    void pushTriangle(const glm::vec3& a,
                      const glm::vec3& b,
                      const glm::vec3& c,
                      std::uint32_t src0,
                      std::uint32_t src1,
                      std::uint32_t src2,
                      const glm::vec2& uv0,
                      const glm::vec2& uv1,
                      const glm::vec2& uv2,
                      const glm::vec3& n0,
                      const glm::vec3& n1,
                      const glm::vec3& n2,
                      const glm::vec3& baseColor0,
                      const glm::vec3& baseColor1,
                      const glm::vec3& baseColor2,
                      std::uint16_t submeshIndex,
                      float alpha,
                      bool doubleSided);

  private:
    Args args_{};
};

} // namespace game::runtime::shared_projected_unit_backend_mesh_submit

