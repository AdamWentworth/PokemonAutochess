#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshFastTriangleAppend.h"

#include <algorithm>
#include <limits>

namespace game::runtime::shared_projected_unit_backend_mesh_fast_triangle_append {

namespace {

glm::vec3 authoredVertexColorFor(const runtime::render_model::MeshData& mesh,
                                 const runtime::render_model::MeshVertex& srcVertex,
                                 bool preserveAuxiliaryVertexColor) {
    if (!mesh.hasVertexColor && !preserveAuxiliaryVertexColor) {
        return glm::vec3(1.0f);
    }
    return glm::clamp(
        glm::vec3(srcVertex.color.r, srcVertex.color.g, srcVertex.color.b),
        0.0f,
        1.0f);
}

float authoredVertexAlphaFor(const runtime::render_model::MeshData& mesh,
                             const runtime::render_model::MeshVertex& srcVertex,
                             bool preserveAuxiliaryVertexColor) {
    if (!mesh.hasVertexColor && !preserveAuxiliaryVertexColor) {
        return 1.0f;
    }
    return std::clamp(srcVertex.color.a, 0.0f, 1.0f);
}

void writeCommonFastVertex(IRenderBackend::WorldMeshVertex& outVertex,
                           const glm::vec3& pos,
                           const runtime::render_model::MeshData& mesh,
                           const runtime::render_model::MeshVertex& srcVertex,
                           bool preserveAuxiliaryVertexColor) {
    outVertex.x = pos.x;
    outVertex.y = pos.y;
    outVertex.z = pos.z;
    outVertex.u = srcVertex.uv.x;
    outVertex.v = srcVertex.uv.y;
    const glm::vec3 authoredVertexColor = authoredVertexColorFor(
        mesh,
        srcVertex,
        preserveAuxiliaryVertexColor);
    const float authoredVertexAlpha = authoredVertexAlphaFor(
        mesh,
        srcVertex,
        preserveAuxiliaryVertexColor);
    outVertex.r = authoredVertexColor.r;
    outVertex.g = authoredVertexColor.g;
    outVertex.b = authoredVertexColor.b;
    outVertex.a = authoredVertexAlpha;
    outVertex.nx = srcVertex.normal.x;
    outVertex.ny = srcVertex.normal.y;
    outVertex.nz = srcVertex.normal.z;
    outVertex.tx = srcVertex.tangent.x;
    outVertex.ty = srcVertex.tangent.y;
    outVertex.tz = srcVertex.tangent.z;
    outVertex.tw = srcVertex.tangent.w;
}

std::uint32_t appendFastVertex(const Args& args,
                               std::uint32_t src,
                               const runtime::render_model::MeshVertex& srcVertex) {
    auto& fastBatch = *args.fastBatch;
    const bool useGpuSkinning = (fastBatch.gpuSkinning != 0u);
    const bool preserveAuxiliaryVertexColor =
        fastBatch.materialMode ==
        game::runtime::render_model::kNativeLayeredUnlitMaterialMode;
    const bool useRigidNodeGpuSkinning =
        args.fastBatchIndex < args.fastBatchUsesRigidNodeGpuSkin->size() &&
        (*args.fastBatchUsesRigidNodeGpuSkin)[args.fastBatchIndex] != 0u;
    const bool canReuseIndexedVertices =
        args.fastBatchIndex < args.modelIndexedVertexRemap->size() &&
        !useRigidNodeGpuSkinning;

    if (useRigidNodeGpuSkinning) {
        if (args.fastBatchIndex >= args.fastBatchRigidNodePaletteIndex->size()) {
            return std::numeric_limits<std::uint32_t>::max();
        }
        const auto paletteIt =
            (*args.fastBatchRigidNodePaletteIndex)[args.fastBatchIndex].find(args.triNodeIndex);
        if (paletteIt == (*args.fastBatchRigidNodePaletteIndex)[args.fastBatchIndex].end()) {
            return std::numeric_limits<std::uint32_t>::max();
        }

        const std::uint16_t rigidPaletteIndex = paletteIt->second;
        const std::uint64_t rigidVertexKey =
            (static_cast<std::uint64_t>(src) << 32u) |
            static_cast<std::uint64_t>(rigidPaletteIndex);
        auto& rigidVertexRemap = (*args.fastBatchRigidVertexRemap)[args.fastBatchIndex];
        const auto existing = rigidVertexRemap.find(rigidVertexKey);
        if (existing != rigidVertexRemap.end()) {
            return existing->second;
        }
        if (fastBatch.vertices.size() >=
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            return std::numeric_limits<std::uint32_t>::max();
        }

        const glm::vec3 pos = args.transforms->resolveDeformedLocalVertexPos(src, srcVertex);
        const std::uint32_t next = static_cast<std::uint32_t>(fastBatch.vertices.size());
        IRenderBackend::WorldMeshVertex outVertex{};
        writeCommonFastVertex(
            outVertex,
            pos,
            *args.mesh,
            srcVertex,
            preserveAuxiliaryVertexColor);
        outVertex.joint0 = static_cast<float>(rigidPaletteIndex);
        outVertex.weight0 = 1.0f;
        outVertex.joint1 = 0.0f;
        outVertex.joint2 = 0.0f;
        outVertex.joint3 = 0.0f;
        outVertex.weight1 = 0.0f;
        outVertex.weight2 = 0.0f;
        outVertex.weight3 = 0.0f;
        fastBatch.vertices.push_back(outVertex);
        rigidVertexRemap.emplace(rigidVertexKey, next);
        return next;
    }

    auto buildVertex = [&](IRenderBackend::WorldMeshVertex& outVertex) {
        const auto surface = useGpuSkinning
            ? shared_projected_unit_backend_mesh_transforms::ModelVertexSurfaceSample{}
            : args.transforms->resolveModelVertexSurface(
                  args.triNodeIndex,
                  src,
                  srcVertex,
                  args.needsLitNormalsForSubmesh,
                  args.needsTangentsForSubmesh);
        const glm::vec3 pos = useGpuSkinning
            ? args.transforms->resolveGpuSkinningInputPos(src, srcVertex)
            : surface.pos;
        writeCommonFastVertex(
            outVertex,
            pos,
            *args.mesh,
            srcVertex,
            preserveAuxiliaryVertexColor);
        if (!useGpuSkinning) {
            if (args.needsLitNormalsForSubmesh) {
                outVertex.nx = surface.normal.x;
                outVertex.ny = surface.normal.y;
                outVertex.nz = surface.normal.z;
            }
            if (args.needsTangentsForSubmesh) {
                outVertex.tx = surface.tangent.x;
                outVertex.ty = surface.tangent.y;
                outVertex.tz = surface.tangent.z;
                outVertex.tw = surface.tangent.w;
            }
        } else {
            outVertex.joint0 = static_cast<float>(srcVertex.j0);
            outVertex.joint1 = static_cast<float>(srcVertex.j1);
            outVertex.joint2 = static_cast<float>(srcVertex.j2);
            outVertex.joint3 = static_cast<float>(srcVertex.j3);
            outVertex.weight0 = srcVertex.w0;
            outVertex.weight1 = srcVertex.w1;
            outVertex.weight2 = srcVertex.w2;
            outVertex.weight3 = srcVertex.w3;
        }
    };

    if (canReuseIndexedVertices && src < (*args.modelIndexedVertexRemap)[args.fastBatchIndex].size()) {
        int& mapped = (*args.modelIndexedVertexRemap)[args.fastBatchIndex][src];
        if (mapped >= 0) {
            return static_cast<std::uint32_t>(mapped);
        }
        if (fastBatch.vertices.size() >=
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            return std::numeric_limits<std::uint32_t>::max();
        }

        const std::uint32_t next = static_cast<std::uint32_t>(fastBatch.vertices.size());
        IRenderBackend::WorldMeshVertex outVertex{};
        buildVertex(outVertex);
        fastBatch.vertices.push_back(outVertex);
        mapped = static_cast<int>(next);
        return next;
    }

    if (fastBatch.vertices.size() >=
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return std::numeric_limits<std::uint32_t>::max();
    }

    const std::uint32_t next = static_cast<std::uint32_t>(fastBatch.vertices.size());
    IRenderBackend::WorldMeshVertex outVertex{};
    buildVertex(outVertex);
    fastBatch.vertices.push_back(outVertex);
    return next;
}

} // namespace

bool appendFastTexturedTriangle(const Args& args) {
    if (!args.mesh || !args.transforms || !args.fastBatch || !args.modelIndexedVertexRemap ||
        !args.fastBatchUsesRigidNodeGpuSkin || !args.fastBatchRigidNodePaletteIndex ||
        !args.fastBatchRigidVertexRemap || !args.v0 || !args.v1 || !args.v2) {
        return false;
    }

    const std::uint32_t outI0 = appendFastVertex(args, args.i0, *args.v0);
    const std::uint32_t outI1 = appendFastVertex(args, args.i1, *args.v1);
    const std::uint32_t outI2 = appendFastVertex(args, args.i2, *args.v2);
    if (outI0 == std::numeric_limits<std::uint32_t>::max() ||
        outI1 == std::numeric_limits<std::uint32_t>::max() ||
        outI2 == std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    args.fastBatch->indices.push_back(outI0);
    args.fastBatch->indices.push_back(outI1);
    args.fastBatch->indices.push_back(outI2);
    return true;
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_fast_triangle_append

