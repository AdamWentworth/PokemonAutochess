#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTriangleLoop.h"

#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshFastTriangleAppend.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshSupport.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTrianglePrep.h"
#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshTriangleSubmit.h"

#include <algorithm>

namespace fast_triangle_append = game::runtime::shared_projected_unit_backend_mesh_fast_triangle_append;
namespace support = game::runtime::shared_projected_unit_backend_mesh_support;
namespace triangle_prep = game::runtime::shared_projected_unit_backend_mesh_triangle_prep;

namespace game::runtime::shared_projected_unit_backend_mesh_triangle_loop {

namespace {

glm::vec3 resolveVertexBaseColor(const runtime::render_model::MeshData& mesh,
                                 const glm::vec3& fallbackBase,
                                 bool texturedSubmesh,
                                 std::size_t triIdx,
                                 std::uint32_t vertexIndex,
                                 const runtime::render_model::MeshVertex& vertex) {
    if (texturedSubmesh) {
        if (mesh.hasVertexColor) {
            return glm::clamp(
                glm::vec3(vertex.color.r, vertex.color.g, vertex.color.b), 0.0f, 1.0f);
        }
        return glm::vec3(1.0f);
    }

    if (mesh.hasVertexColor) {
        return glm::clamp(
            glm::vec3(vertex.color.r, vertex.color.g, vertex.color.b), 0.0f, 1.0f);
    }
    if (triIdx < mesh.triangleSubmesh.size() && !mesh.submeshBaseColors.empty()) {
        const std::uint16_t submeshIndex = mesh.triangleSubmesh[triIdx];
        if (submeshIndex < mesh.submeshBaseColors.size()) {
            const glm::vec4 subColor = mesh.submeshBaseColors[submeshIndex];
            return glm::clamp(glm::vec3(subColor.r, subColor.g, subColor.b), 0.0f, 1.0f);
        }
    }
    (void)vertexIndex;
    return fallbackBase;
}

} // namespace

void appendFallbackTriangles(const Args& args) {
    if (!args.renderArgs || !args.prep || !args.transforms || !args.renderArgs->projectedDebug ||
        !args.renderArgs->modelDepthTris || !args.renderArgs->world3DTriangles) {
        return;
    }

    auto& prep = *args.prep;
    if (!prep.mesh) {
        return;
    }

    const auto& renderArgs = *args.renderArgs;
    const auto* mesh = prep.mesh;
    auto& transforms = *args.transforms;
    auto& modelIndexedBatchesPerSubmesh = prep.modelIndexedBatchesPerSubmesh;
    auto& modelIndexedVertexRemap = prep.modelIndexedVertexRemap;

    shared_projected_unit_backend_mesh_submit::TriangleSubmitter triangleSubmitter;
    triangleSubmitter.initialize(
        shared_projected_unit_backend_mesh_submit::TriangleSubmitter::Args{
            renderArgs.supportsWorldTriangles3D,
            prep.useIndexedWorldModelPath,
            prep.fullIndexedMeshPath,
            renderArgs.backendModelFastTexturedPathEnabled(),
            renderArgs.backendModelBackfaceCullingEnabled(),
            args.cameraWorldPos,
            prep.lightDir,
            renderArgs.projectedDebug,
            &modelIndexedBatchesPerSubmesh,
            &modelIndexedVertexRemap,
            renderArgs.modelDepthTris,
            renderArgs.world3DTriangles});

    if (prep.useFastTexturedFullMeshPath &&
        modelIndexedVertexRemap.empty() &&
        !mesh->vertices.empty() &&
        !modelIndexedBatchesPerSubmesh.empty()) {
        modelIndexedVertexRemap.resize(modelIndexedBatchesPerSubmesh.size());
        for (auto& remap : modelIndexedVertexRemap) {
            remap.assign(mesh->vertices.size(), -1);
        }
    }

    auto& triNodeIndexByTriangle = support::triNodeIndexByTriangleScratch();
    triangle_prep::State trianglePrepState{};
    triangle_prep::initializeIndexedTrianglePrep(
        {
            .mesh = mesh,
            .submeshNodeFallback = prep.submeshNodeFallback,
            .nodeGlobals = (prep.scenePose && prep.scenePose->hasScenePose)
                ? &prep.scenePose->nodeGlobals
                : &mesh->bindNodeGlobals,
            .modelMatrix = &prep.modelM,
            .triangleCount = prep.triangleCount,
            .useFastTexturedFullMeshPath = prep.useFastTexturedFullMeshPath,
            .enableGpuClipSkinning = args.enableGpuClipSkinning,
            .triNodeIndexByTriangle = &triNodeIndexByTriangle,
            .modelIndexedBatchesPerSubmesh = &modelIndexedBatchesPerSubmesh,
        },
        trianglePrepState);
    auto& fastBatchUsesRigidNodeGpuSkin = trianglePrepState.fastBatchUsesRigidNodeGpuSkin;
    auto& fastBatchRigidNodePaletteIndex = trianglePrepState.fastBatchRigidNodePaletteIndex;
    auto& fastBatchRigidVertexRemap = trianglePrepState.fastBatchRigidVertexRemap;

    std::size_t previousTriSample = prep.triangleCount;
    for (std::size_t sampleIdx = 0; sampleIdx < prep.effectiveUnitTriangleBudget; ++sampleIdx) {
        std::size_t triIdx = sampleIdx;
        if (prep.downsampleModelTriangles) {
            triIdx = support::selectUniformTriangleIndex(
                sampleIdx,
                prep.effectiveUnitTriangleBudget,
                prep.triangleCount);
            if (triIdx == previousTriSample && triIdx + 1u < prep.triangleCount) {
                ++triIdx;
            }
        }
        previousTriSample = triIdx;

        const std::size_t baseIndex = triIdx * 3u;
        const std::uint32_t i0 = mesh->indices[baseIndex + 0];
        const std::uint32_t i1 = mesh->indices[baseIndex + 1];
        const std::uint32_t i2 = mesh->indices[baseIndex + 2];
        if (i0 >= mesh->vertices.size() || i1 >= mesh->vertices.size() ||
            i2 >= mesh->vertices.size()) {
            continue;
        }

        const auto& v0 = mesh->vertices[i0];
        const auto& v1 = mesh->vertices[i1];
        const auto& v2 = mesh->vertices[i2];
        const int triNodeIndex = triNodeIndexByTriangle[triIdx];
        const std::uint16_t triSubmeshIndex =
            (triIdx < mesh->triangleSubmesh.size())
                ? mesh->triangleSubmesh[triIdx]
                : static_cast<std::uint16_t>(0u);

        bool needsLitNormalsForSubmesh = true;
        bool needsTangentsForSubmesh = true;
        if (prep.useIndexedWorldModelPath && !modelIndexedBatchesPerSubmesh.empty()) {
            std::size_t submeshBatchIndex = static_cast<std::size_t>(triSubmeshIndex);
            if (submeshBatchIndex >= modelIndexedBatchesPerSubmesh.size()) {
                submeshBatchIndex = 0u;
            }
            const auto& submeshBatch = modelIndexedBatchesPerSubmesh[submeshBatchIndex];
            const auto& materialBatch = shared_world_batches::resolvedMaterialBatch(submeshBatch);
            needsLitNormalsForSubmesh = materialBatch.materialMode >= 2u;
            const bool hasNormalTexture =
                shared_world_batches::resolvedHasNormalTexture(submeshBatch);
            needsTangentsForSubmesh = needsLitNormalsForSubmesh && hasNormalTexture;
        }

        const bool texturedSubmesh =
            prep.useIndexedWorldModelPath &&
            static_cast<std::size_t>(triSubmeshIndex) < modelIndexedBatchesPerSubmesh.size() &&
            shared_world_batches::resolvedHasBaseTexture(
                modelIndexedBatchesPerSubmesh[static_cast<std::size_t>(triSubmeshIndex)]);
        if (prep.useFastTexturedFullMeshPath && texturedSubmesh) {
            std::size_t fastBatchIndex = static_cast<std::size_t>(triSubmeshIndex);
            if (fastBatchIndex >= modelIndexedBatchesPerSubmesh.size()) {
                fastBatchIndex = 0u;
            }
            auto& fastBatch = modelIndexedBatchesPerSubmesh[fastBatchIndex];
            fastBatch.vertexColorMulR = prep.fastTexturedTint.r;
            fastBatch.vertexColorMulG = prep.fastTexturedTint.g;
            fastBatch.vertexColorMulB = prep.fastTexturedTint.b;
            fastBatch.vertexColorMulA = prep.fastTexturedAlpha;
            if (fast_triangle_append::appendFastTexturedTriangle(
                    {
                        .mesh = mesh,
                        .transforms = &transforms,
                        .fastBatch = &fastBatch,
                        .modelIndexedVertexRemap = &modelIndexedVertexRemap,
                        .fastBatchUsesRigidNodeGpuSkin = &fastBatchUsesRigidNodeGpuSkin,
                        .fastBatchRigidNodePaletteIndex = &fastBatchRigidNodePaletteIndex,
                        .fastBatchRigidVertexRemap = &fastBatchRigidVertexRemap,
                        .fastBatchIndex = fastBatchIndex,
                        .triNodeIndex = triNodeIndex,
                        .needsLitNormalsForSubmesh = needsLitNormalsForSubmesh,
                        .needsTangentsForSubmesh = needsTangentsForSubmesh,
                        .i0 = i0,
                        .i1 = i1,
                        .i2 = i2,
                        .v0 = &v0,
                        .v1 = &v1,
                        .v2 = &v2,
                    })) {
                continue;
            }
            continue;
        }

        const float triOpacity =
            (triIdx < mesh->triangleOpacity.size()) ? mesh->triangleOpacity[triIdx] : 1.0f;
        const float alphaBase = std::clamp(args.modelFadeAlpha, 0.0f, 1.0f);
        const float alpha = texturedSubmesh
            ? alphaBase
            : alphaBase * std::clamp(triOpacity, 0.0f, 1.0f);
        if (alpha < 0.03f && !texturedSubmesh) {
            continue;
        }

        const bool triDoubleSided =
            (triIdx < mesh->triangleDoubleSided.size()) && (mesh->triangleDoubleSided[triIdx] != 0u);

        glm::vec3 a(0.0f);
        glm::vec3 b(0.0f);
        glm::vec3 c(0.0f);
        glm::vec3 n0(0.0f, 1.0f, 0.0f);
        glm::vec3 n1(0.0f, 1.0f, 0.0f);
        glm::vec3 n2(0.0f, 1.0f, 0.0f);
        glm::vec4 t0(0.0f, 0.0f, 0.0f, 1.0f);
        glm::vec4 t1(0.0f, 0.0f, 0.0f, 1.0f);
        glm::vec4 t2(0.0f, 0.0f, 0.0f, 1.0f);
        if (prep.useIndexedWorldModelPath) {
            const auto s0 = transforms.resolveModelVertexSurface(
                triNodeIndex,
                i0,
                v0,
                needsLitNormalsForSubmesh,
                needsTangentsForSubmesh);
            const auto s1 = transforms.resolveModelVertexSurface(
                triNodeIndex,
                i1,
                v1,
                needsLitNormalsForSubmesh,
                needsTangentsForSubmesh);
            const auto s2 = transforms.resolveModelVertexSurface(
                triNodeIndex,
                i2,
                v2,
                needsLitNormalsForSubmesh,
                needsTangentsForSubmesh);
            a = s0.pos;
            b = s1.pos;
            c = s2.pos;
            if (needsLitNormalsForSubmesh) {
                n0 = s0.normal;
                n1 = s1.normal;
                n2 = s2.normal;
            } else {
                n0 = v0.normal;
                n1 = v1.normal;
                n2 = v2.normal;
            }
            if (needsTangentsForSubmesh) {
                t0 = s0.tangent;
                t1 = s1.tangent;
                t2 = s2.tangent;
            } else {
                t0 = v0.tangent;
                t1 = v1.tangent;
                t2 = v2.tangent;
            }
        } else {
            const auto sk0 = transforms.resolveWorldVertex(triNodeIndex, i0, v0);
            const auto sk1 = transforms.resolveWorldVertex(triNodeIndex, i1, v1);
            const auto sk2 = transforms.resolveWorldVertex(triNodeIndex, i2, v2);
            a = sk0.pos;
            b = sk1.pos;
            c = sk2.pos;
            n0 = sk0.normal;
            n1 = sk1.normal;
            n2 = sk2.normal;
            t0 = v0.tangent;
            t1 = v1.tangent;
            t2 = v2.tangent;
        }

        glm::vec3 baseColor0 = resolveVertexBaseColor(
            *mesh, prep.fallbackBase, texturedSubmesh, triIdx, i0, v0);
        glm::vec3 baseColor1 = resolveVertexBaseColor(
            *mesh, prep.fallbackBase, texturedSubmesh, triIdx, i1, v1);
        glm::vec3 baseColor2 = resolveVertexBaseColor(
            *mesh, prep.fallbackBase, texturedSubmesh, triIdx, i2, v2);
        if (!args.strictGltfParity && args.captureVisualTintStrength > 0.001f) {
            const float tintAmt = std::clamp(args.captureVisualTintStrength, 0.0f, 1.0f);
            baseColor0 = glm::mix(baseColor0, args.captureTintColor, tintAmt);
            baseColor1 = glm::mix(baseColor1, args.captureTintColor, tintAmt);
            baseColor2 = glm::mix(baseColor2, args.captureTintColor, tintAmt);
        }

        triangleSubmitter.pushTriangle(
            a,
            b,
            c,
            i0,
            i1,
            i2,
            v0.uv,
            v1.uv,
            v2.uv,
            n0,
            n1,
            n2,
            t0,
            t1,
            t2,
            baseColor0,
            baseColor1,
            baseColor2,
            triSubmeshIndex,
            alpha,
            triDoubleSided);
    }
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_triangle_loop

