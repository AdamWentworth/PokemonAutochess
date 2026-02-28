#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTriangleSubmit.h"
#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.h"

#include "game/runtime/BackendUnitVisuals.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

namespace {
std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::size_t selectUniformTriangleIndex(std::size_t sampleIndex,
                                       std::size_t sampleCount,
                                       std::size_t triangleCount) {
    if (triangleCount == 0u || sampleCount == 0u) return 0u;
    if (sampleCount >= triangleCount) return std::min(sampleIndex, triangleCount - 1u);
    const double t = (static_cast<double>(sampleIndex) + 0.5) /
                     static_cast<double>(sampleCount);
    const std::size_t idx =
        static_cast<std::size_t>(t * static_cast<double>(triangleCount));
    return std::min(idx, triangleCount - 1u);
}
} // namespace

namespace game::runtime::shared_projected_unit_backend_mesh {

Result renderProjectedUnitBackendMesh(const Args& args) {
    Result out{};
    if (!args.dataDb || !args.unit || !args.pose || !args.meshForUnit || !args.scenePose ||
        !args.tint || !args.projectedDebug || !args.sharedTailFireAnchors ||
        !args.worldIndexedBatches || !args.modelDepthTris || !args.modelDepthWorldTris ||
        !args.remainingModelTrianglesBudget || !args.world3DTriangles ||
        !args.backendModelTriangleLimit || !args.backendModelFullMeshEnabled ||
        !args.backendModelFastTexturedPathEnabled || !args.backendModelBackfaceCullingEnabled) {
        return out;
    }

    const auto& unit = *args.unit;
    const auto* meshForUnit = args.meshForUnit;

    const float captureVisualTintStrength = args.captureVisualTintStrength;
    const float modelFadeAlpha = args.modelFadeAlpha;
    const glm::vec3 captureTintColor = args.captureTintColor;
    const glm::vec3 cameraWorldPos = args.cameraWorldPos;

    const bool supportsWorldTriangles3D = args.supportsWorldTriangles3D;

    auto& projectedDebug = *args.projectedDebug;
    auto& sharedTailFireAnchors = *args.sharedTailFireAnchors;
    auto& worldIndexedBatches = *args.worldIndexedBatches;
    auto& modelDepthTris = *args.modelDepthTris;
    auto& modelDepthWorldTris = *args.modelDepthWorldTris;
    auto& world3DTriangles = *args.world3DTriangles;

    const auto& backendModelFastTexturedPathEnabled = args.backendModelFastTexturedPathEnabled;
    const auto& backendModelBackfaceCullingEnabled = args.backendModelBackfaceCullingEnabled;

    using SharedTailFireAnchor = game::runtime::shared_tail_fire_fallback::Anchor;

    bool drewModelMesh = false;
    if (meshForUnit) {
        shared_projected_unit_backend_mesh_prep::PreparedState prep;
        if (!shared_projected_unit_backend_mesh_prep::prepareProjectedUnitBackendMesh(args, out, prep)) {
            return out;
        }

        const runtime::backend_model::MeshData* mesh = prep.mesh;
        const std::size_t triangleCount = prep.triangleCount;
        const std::size_t effectiveUnitTriangleBudget = prep.effectiveUnitTriangleBudget;
        const bool useIndexedWorldModelPath = prep.useIndexedWorldModelPath;
        const bool fullIndexedMeshPath = prep.fullIndexedMeshPath;
        const bool useFastTexturedFullMeshPath = prep.useFastTexturedFullMeshPath;
        const float resolvedScaleCorrection = prep.resolvedScaleCorrection;
        const std::size_t modelDepthCountBefore = prep.modelDepthCountBefore;
        const std::size_t modelDepthWorldCountBefore = prep.modelDepthWorldCountBefore;
        const std::size_t world3DTriangleCountBefore = prep.world3DTriangleCountBefore;
        auto& submeshNodeFallback = prep.submeshNodeFallback;
        auto& modelIndexedBatchesPerSubmesh = prep.modelIndexedBatchesPerSubmesh;
        auto& modelIndexedVertexRemap = prep.modelIndexedVertexRemap;
        const auto& nodeGlobals =
            prep.scenePose.hasScenePose ? prep.scenePose.nodeGlobals : mesh->bindNodeGlobals;
        const glm::vec3& lightDir = prep.lightDir;
        const glm::vec3& fallbackBase = prep.fallbackBase;
        const bool downsampleModelTriangles = prep.downsampleModelTriangles;
        const float fastTexturedAlpha = prep.fastTexturedAlpha;
        const glm::vec3& fastTexturedTint = prep.fastTexturedTint;
        shared_projected_unit_backend_mesh_transforms::Resolver transforms;
        transforms.initialize(args, prep);
        shared_projected_unit_backend_mesh_submit::TriangleSubmitter triangleSubmitter;
        triangleSubmitter.initialize(
            shared_projected_unit_backend_mesh_submit::TriangleSubmitter::Args{
                supportsWorldTriangles3D,
                useIndexedWorldModelPath,
                fullIndexedMeshPath,
                backendModelFastTexturedPathEnabled(),
                backendModelBackfaceCullingEnabled(),
                cameraWorldPos,
                lightDir,
                &projectedDebug,
                &modelIndexedBatchesPerSubmesh,
                &modelIndexedVertexRemap,
                &modelDepthTris,
                &world3DTriangles});

        const auto resolveTriNodeIndex = [&](std::size_t triIdx) -> int {
            int triNodeIndex =
                (triIdx < mesh->triangleNodeIndex.size())
                    ? mesh->triangleNodeIndex[triIdx]
                    : -1;
            if (triNodeIndex < 0 &&
                triIdx < mesh->triangleSubmesh.size() &&
                !submeshNodeFallback.empty()) {
                const std::uint16_t submeshIndex = mesh->triangleSubmesh[triIdx];
                if (submeshIndex < submeshNodeFallback.size()) {
                    triNodeIndex = submeshNodeFallback[submeshIndex];
                }
            }
            return triNodeIndex;
        };

        if (args.enableGpuClipSkinning && useFastTexturedFullMeshPath &&
            !modelIndexedBatchesPerSubmesh.empty()) {
            std::vector<int> skinningNodeByBatch(
                modelIndexedBatchesPerSubmesh.size(),
                std::numeric_limits<int>::min());
            std::vector<std::uint8_t> skinningEligibleByBatch(
                modelIndexedBatchesPerSubmesh.size(),
                1u);

            for (std::size_t triIdx = 0; triIdx < triangleCount; ++triIdx) {
                if (triIdx >= mesh->triangleSubmesh.size()) continue;
                const std::size_t batchIndex =
                    static_cast<std::size_t>(mesh->triangleSubmesh[triIdx]);
                if (batchIndex >= modelIndexedBatchesPerSubmesh.size()) continue;
                if (skinningEligibleByBatch[batchIndex] == 0u) continue;

                const int triNodeIndex = resolveTriNodeIndex(triIdx);
                if (triNodeIndex < 0) {
                    skinningEligibleByBatch[batchIndex] = 0u;
                    continue;
                }

                const int existingNode = skinningNodeByBatch[batchIndex];
                if (existingNode == std::numeric_limits<int>::min()) {
                    skinningNodeByBatch[batchIndex] = triNodeIndex;
                } else if (existingNode != triNodeIndex) {
                    // Mixed-node batches are rare and currently not supported by this GPU skinning path.
                    skinningEligibleByBatch[batchIndex] = 0u;
                }
            }

            for (std::size_t batchIndex = 0; batchIndex < modelIndexedBatchesPerSubmesh.size();
                 ++batchIndex) {
                auto& batch = modelIndexedBatchesPerSubmesh[batchIndex];
                batch.gpuSkinning = 0u;
                batch.skinMatrixCount = 0u;
                batch.skinMatrices.clear();
                if (skinningEligibleByBatch[batchIndex] == 0u) continue;

                const int nodeIndex = skinningNodeByBatch[batchIndex];
                if (nodeIndex == std::numeric_limits<int>::min()) continue;

                if (!transforms.configureGpuClipSkinningBatch(
                        nodeIndex,
                        batch.modelMatrix,
                        batch.skinMatrices,
                        batch.skinMatrixCount)) {
                    continue;
                }
                batch.gpuSkinning = 1u;
            }
        }

        if (unit.alive && !unit.fainting && toLowerCopy(unit.name) == "charmander") {
            const TailFireVFX::Config& tailCfg =
                game::runtime::shared_projected_scene::getTailFireFallbackCfg();
            const int tailNodeIndex = tailCfg.tailTipNodeIndex;
            if (tailNodeIndex >= 0 &&
                static_cast<std::size_t>(tailNodeIndex) < nodeGlobals.size()) {
                const glm::mat4& tailWorldM = transforms.worldMatrixForNode(tailNodeIndex);

                auto safeNorm = [](glm::vec3 v, const glm::vec3& fallback) {
                    const float len2 = glm::dot(v, v);
                    if (len2 <= 1e-10f) return fallback;
                    return v * (1.0f / std::sqrt(len2));
                };
                glm::vec3 bx = safeNorm(glm::vec3(tailWorldM[0]), glm::vec3(1.0f, 0.0f, 0.0f));
                glm::vec3 by = glm::vec3(tailWorldM[1]);
                by = by - bx * glm::dot(by, bx);
                by = safeNorm(by, glm::vec3(0.0f, 1.0f, 0.0f));
                glm::vec3 bz = safeNorm(glm::cross(bx, by), glm::vec3(0.0f, 0.0f, 1.0f));
                if (glm::dot(glm::cross(bx, by), bz) < 0.0f) {
                    bz = -bz;
                }
                const glm::mat3 tailBasis(bx, by, bz);
                glm::vec3 backDirWorld = tailBasis * tailCfg.backDir;
                backDirWorld = safeNorm(backDirWorld, glm::vec3(0.0f, 1.0f, 0.0f));

                SharedTailFireAnchor& anchor = sharedTailFireAnchors[unit.id];
                anchor.valid = true;
                anchor.pos = glm::vec3(tailWorldM[3]) + glm::vec3(0.0f, tailCfg.tailWorldYOffset, 0.0f);
                anchor.basis = tailBasis;
                anchor.backDir = backDirWorld;
                anchor.particleSizeScale =
                    std::max(0.01f, std::max(0.01f, mesh->modelScaleFactor) * resolvedScaleCorrection);
            }
        }

        std::size_t previousTriSample = triangleCount;
        for (std::size_t sampleIdx = 0; sampleIdx < effectiveUnitTriangleBudget; ++sampleIdx) {
            std::size_t triIdx = sampleIdx;
            if (downsampleModelTriangles) {
                triIdx =
                    selectUniformTriangleIndex(sampleIdx, effectiveUnitTriangleBudget, triangleCount);
                if (triIdx == previousTriSample && triIdx + 1u < triangleCount) ++triIdx;
            }
            previousTriSample = triIdx;

            const std::size_t i = triIdx * 3u;
            const std::uint32_t i0 = mesh->indices[i + 0];
            const std::uint32_t i1 = mesh->indices[i + 1];
            const std::uint32_t i2 = mesh->indices[i + 2];
            if (i0 >= mesh->vertices.size() ||
                i1 >= mesh->vertices.size() ||
                i2 >= mesh->vertices.size()) {
                continue;
            }

            const auto& v0 = mesh->vertices[i0];
            const auto& v1 = mesh->vertices[i1];
            const auto& v2 = mesh->vertices[i2];

            const int triNodeIndex = resolveTriNodeIndex(triIdx);

            const std::uint16_t triSubmeshIndex =
                (triIdx < mesh->triangleSubmesh.size())
                    ? mesh->triangleSubmesh[triIdx]
                    : static_cast<std::uint16_t>(0u);
            const bool texturedSubmesh =
                useIndexedWorldModelPath &&
                static_cast<std::size_t>(triSubmeshIndex) <
                    modelIndexedBatchesPerSubmesh.size() &&
                modelIndexedBatchesPerSubmesh[static_cast<std::size_t>(triSubmeshIndex)]
                        .textureRgba != nullptr &&
                modelIndexedBatchesPerSubmesh[static_cast<std::size_t>(triSubmeshIndex)]
                        .textureWidth > 0 &&
                modelIndexedBatchesPerSubmesh[static_cast<std::size_t>(triSubmeshIndex)]
                        .textureHeight > 0;
            if (useFastTexturedFullMeshPath && texturedSubmesh) {
                std::size_t fastBatchIndex = static_cast<std::size_t>(triSubmeshIndex);
                if (fastBatchIndex >= modelIndexedBatchesPerSubmesh.size()) fastBatchIndex = 0u;
                auto& fastBatch = modelIndexedBatchesPerSubmesh[fastBatchIndex];
                const bool useGpuSkinning = (fastBatch.gpuSkinning != 0u);
                const bool canReuseIndexedVertices =
                    fastBatchIndex < modelIndexedVertexRemap.size();
                const auto appendFastVertex = [&](std::uint32_t src,
                                                  const runtime::backend_model::MeshVertex& srcVertex)
                    -> std::uint32_t {
                    if (canReuseIndexedVertices &&
                        src < modelIndexedVertexRemap[fastBatchIndex].size()) {
                        int& mapped = modelIndexedVertexRemap[fastBatchIndex][src];
                        if (mapped >= 0) {
                            return static_cast<std::uint32_t>(mapped);
                        }
                        if (fastBatch.vertices.size() >=
                            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                            return std::numeric_limits<std::uint32_t>::max();
                        }
                        const glm::vec3 pos = useGpuSkinning
                            ? transforms.resolveGpuSkinningInputPos(src, srcVertex)
                            : transforms.resolveWorldVertexPos(triNodeIndex, src, srcVertex);
                        const std::uint32_t next =
                            static_cast<std::uint32_t>(fastBatch.vertices.size());
                        IRenderBackend::WorldMeshVertex outVertex{};
                        outVertex.x = pos.x;
                        outVertex.y = pos.y;
                        outVertex.z = pos.z;
                        outVertex.u = srcVertex.uv.x;
                        outVertex.v = srcVertex.uv.y;
                        outVertex.r = fastTexturedTint.r;
                        outVertex.g = fastTexturedTint.g;
                        outVertex.b = fastTexturedTint.b;
                        outVertex.a = fastTexturedAlpha;
                        if (useGpuSkinning) {
                            outVertex.nx = srcVertex.normal.x;
                            outVertex.ny = srcVertex.normal.y;
                            outVertex.nz = srcVertex.normal.z;
                        } else {
                            const glm::vec3 nrm =
                                transforms.resolveModelVertexNormal(triNodeIndex, src, srcVertex);
                            outVertex.nx = nrm.x;
                            outVertex.ny = nrm.y;
                            outVertex.nz = nrm.z;
                        }
                        if (useGpuSkinning) {
                            outVertex.joint0 = static_cast<float>(srcVertex.j0);
                            outVertex.joint1 = static_cast<float>(srcVertex.j1);
                            outVertex.joint2 = static_cast<float>(srcVertex.j2);
                            outVertex.joint3 = static_cast<float>(srcVertex.j3);
                            outVertex.weight0 = srcVertex.w0;
                            outVertex.weight1 = srcVertex.w1;
                            outVertex.weight2 = srcVertex.w2;
                            outVertex.weight3 = srcVertex.w3;
                        }
                        fastBatch.vertices.push_back(outVertex);
                        mapped = static_cast<int>(next);
                        return next;
                    }
                    if (fastBatch.vertices.size() >=
                        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                        return std::numeric_limits<std::uint32_t>::max();
                    }
                    const glm::vec3 pos = useGpuSkinning
                        ? transforms.resolveGpuSkinningInputPos(src, srcVertex)
                        : transforms.resolveWorldVertexPos(triNodeIndex, src, srcVertex);
                    const std::uint32_t next =
                        static_cast<std::uint32_t>(fastBatch.vertices.size());
                    IRenderBackend::WorldMeshVertex outVertex{};
                    outVertex.x = pos.x;
                    outVertex.y = pos.y;
                    outVertex.z = pos.z;
                    outVertex.u = srcVertex.uv.x;
                    outVertex.v = srcVertex.uv.y;
                    outVertex.r = fastTexturedTint.r;
                    outVertex.g = fastTexturedTint.g;
                    outVertex.b = fastTexturedTint.b;
                    outVertex.a = fastTexturedAlpha;
                    if (useGpuSkinning) {
                        outVertex.nx = srcVertex.normal.x;
                        outVertex.ny = srcVertex.normal.y;
                        outVertex.nz = srcVertex.normal.z;
                    } else {
                        const glm::vec3 nrm =
                            transforms.resolveModelVertexNormal(triNodeIndex, src, srcVertex);
                        outVertex.nx = nrm.x;
                        outVertex.ny = nrm.y;
                        outVertex.nz = nrm.z;
                    }
                    if (useGpuSkinning) {
                        outVertex.joint0 = static_cast<float>(srcVertex.j0);
                        outVertex.joint1 = static_cast<float>(srcVertex.j1);
                        outVertex.joint2 = static_cast<float>(srcVertex.j2);
                        outVertex.joint3 = static_cast<float>(srcVertex.j3);
                        outVertex.weight0 = srcVertex.w0;
                        outVertex.weight1 = srcVertex.w1;
                        outVertex.weight2 = srcVertex.w2;
                        outVertex.weight3 = srcVertex.w3;
                    }
                    fastBatch.vertices.push_back(outVertex);
                    return next;
                };

                const std::uint32_t outI0 = appendFastVertex(i0, v0);
                const std::uint32_t outI1 = appendFastVertex(i1, v1);
                const std::uint32_t outI2 = appendFastVertex(i2, v2);
                if (outI0 == std::numeric_limits<std::uint32_t>::max() ||
                    outI1 == std::numeric_limits<std::uint32_t>::max() ||
                    outI2 == std::numeric_limits<std::uint32_t>::max()) {
                    continue;
                }
                fastBatch.indices.push_back(outI0);
                fastBatch.indices.push_back(outI1);
                fastBatch.indices.push_back(outI2);
                continue;
            }

            const float triOpacity = (triIdx < mesh->triangleOpacity.size())
                ? mesh->triangleOpacity[triIdx]
                : 1.0f;
            // Textured indexed batches apply alpha in the pixel shader.
            // Avoid pre-multiplying with sampled triangle opacity (which would double-attenuate).
            const float alphaBase = std::clamp(modelFadeAlpha, 0.0f, 1.0f);
            const float alpha = texturedSubmesh
                ? alphaBase
                : alphaBase * std::clamp(triOpacity, 0.0f, 1.0f);
            if (alpha < 0.03f && !texturedSubmesh) continue;
            const bool triDoubleSided =
                (triIdx < mesh->triangleDoubleSided.size()) &&
                (mesh->triangleDoubleSided[triIdx] != 0u);

            glm::vec3 a(0.0f);
            glm::vec3 b(0.0f);
            glm::vec3 c(0.0f);
            glm::vec3 n0(0.0f, 1.0f, 0.0f);
            glm::vec3 n1(0.0f, 1.0f, 0.0f);
            glm::vec3 n2(0.0f, 1.0f, 0.0f);
            if (useIndexedWorldModelPath) {
                a = transforms.resolveWorldVertexPos(triNodeIndex, i0, v0);
                b = transforms.resolveWorldVertexPos(triNodeIndex, i1, v1);
                c = transforms.resolveWorldVertexPos(triNodeIndex, i2, v2);
                n0 = transforms.resolveModelVertexNormal(triNodeIndex, i0, v0);
                n1 = transforms.resolveModelVertexNormal(triNodeIndex, i1, v1);
                n2 = transforms.resolveModelVertexNormal(triNodeIndex, i2, v2);
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
            }

            glm::vec3 baseColor0 = fallbackBase;
            glm::vec3 baseColor1 = fallbackBase;
            glm::vec3 baseColor2 = fallbackBase;
            auto resolveVertexBase = [&](std::uint32_t vi,
                                         const runtime::backend_model::MeshVertex& v) {
                if (mesh->hasVertexBaseColor && vi < mesh->vertexBaseColors.size()) {
                    return glm::clamp(mesh->vertexBaseColors[vi], 0.0f, 1.0f);
                }
                if (mesh->hasVertexColor) {
                    return glm::clamp(
                        glm::vec3(v.color.r, v.color.g, v.color.b), 0.0f, 1.0f);
                }
                if (triIdx < mesh->triangleBaseColors.size()) {
                    return glm::clamp(mesh->triangleBaseColors[triIdx], 0.0f, 1.0f);
                }
                if (triIdx < mesh->triangleSubmesh.size() &&
                    !mesh->submeshBaseColors.empty()) {
                    const std::uint16_t submeshIndex = mesh->triangleSubmesh[triIdx];
                    if (submeshIndex < mesh->submeshBaseColors.size()) {
                        const glm::vec4 subColor = mesh->submeshBaseColors[submeshIndex];
                        return glm::clamp(
                            glm::vec3(subColor.r, subColor.g, subColor.b), 0.0f, 1.0f);
                    }
                }
                (void)vi;
                return fallbackBase;
            };
            baseColor0 = resolveVertexBase(i0, v0);
            baseColor1 = resolveVertexBase(i1, v1);
            baseColor2 = resolveVertexBase(i2, v2);
            if (captureVisualTintStrength > 0.001f) {
                const float tintAmt = std::clamp(captureVisualTintStrength, 0.0f, 1.0f);
                baseColor0 = glm::mix(baseColor0, captureTintColor, tintAmt);
                baseColor1 = glm::mix(baseColor1, captureTintColor, tintAmt);
                baseColor2 = glm::mix(baseColor2, captureTintColor, tintAmt);
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
                baseColor0,
                baseColor1,
                baseColor2,
                triSubmeshIndex,
                alpha,
                triDoubleSided);
        }
        bool queuedIndexedBatch = false;
        if (useIndexedWorldModelPath && !modelIndexedBatchesPerSubmesh.empty()) {
            for (auto& batch : modelIndexedBatchesPerSubmesh) {
                if (batch.vertices.empty() || batch.indices.empty()) continue;
                worldIndexedBatches.push_back(std::move(batch));
                queuedIndexedBatch = true;
            }
        }

        drewModelMesh = runtime::backend_units::didAccumulateModelGeometry(
            modelDepthCountBefore,
            modelDepthTris.size(),
            modelDepthWorldCountBefore,
            modelDepthWorldTris.size()) ||
            (world3DTriangles.size() > world3DTriangleCountBefore) ||
            queuedIndexedBatch;
    }
    out.drewModelMesh = drewModelMesh;
    return out;
}

} // namespace game::runtime::shared_projected_unit_backend_mesh


