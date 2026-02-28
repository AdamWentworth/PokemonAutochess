#pragma once

#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.h"

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_projected_unit_backend_mesh_transforms {

struct WorldVertexSample {
    glm::vec3 pos{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

class Resolver {
  public:
    void initialize(const shared_projected_unit_backend_mesh::Args& args,
                    const shared_projected_unit_backend_mesh_prep::PreparedState& prep);

    const glm::mat4& worldMatrixForNode(int triNodeIndex);
    const glm::mat3& worldNormalMatrixForNode(int triNodeIndex);

    WorldVertexSample resolveWorldVertex(int triNodeIndex,
                                         std::uint32_t vertexIndex,
                                         const runtime::backend_model::MeshVertex& vtx);
    glm::vec3 resolveWorldVertexPos(int triNodeIndex,
                                    std::uint32_t vertexIndex,
                                    const runtime::backend_model::MeshVertex& vtx);
    glm::vec3 resolveModelVertexNormal(int triNodeIndex,
                                       std::uint32_t vertexIndex,
                                       const runtime::backend_model::MeshVertex& vtx);
    glm::vec3 resolveGpuSkinningInputPos(std::uint32_t vertexIndex,
                                         const runtime::backend_model::MeshVertex& vtx);
    bool configureGpuClipSkinningBatch(int triNodeIndex,
                                       std::array<float, 16>& inOutModelMatrix,
                                       std::vector<float>& outSkinMatrices,
                                       std::uint32_t& outSkinMatrixCount);

  private:
    struct SkinResult {
        glm::vec3 pos{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        bool applied = false;
    };

    const std::vector<glm::mat4>* ensureSkinMatricesForNode(int nodeIndex);
    SkinResult skinVertexAtNode(int nodeIndex,
                                const runtime::backend_model::MeshVertex& vtx,
                                const glm::vec3& localPos,
                                const glm::vec3& localNormal);
    glm::vec3 skinPositionAtNode(int nodeIndex,
                                 const runtime::backend_model::MeshVertex& vtx,
                                 const glm::vec3& localPos);

    std::size_t nodeTransformIndexFor(int triNodeIndex) const;

    const shared_projected_unit_backend_mesh::Args* renderArgs_ = nullptr;
    const shared_projected_unit_backend_mesh_prep::PreparedState* prep_ = nullptr;
    const runtime::backend_model::MeshData* mesh_ = nullptr;
    const PokemonInstance* unit_ = nullptr;
    const runtime::backend_anim::ProceduralPose* pose_ = nullptr;
    const std::vector<glm::mat4>* nodeGlobals_ = nullptr;

    glm::mat4 modelM_{1.0f};
    float worldCellSize_ = 1.0f;
    bool hasClipPose_ = false;
    bool usePositionOnlyVertexPath_ = false;
    bool clipSkinningEnabled_ = true;
    bool gpuClipSkinningRequested_ = false;
    std::size_t nodeCount_ = 0u;
};

} // namespace game::runtime::shared_projected_unit_backend_mesh_transforms
