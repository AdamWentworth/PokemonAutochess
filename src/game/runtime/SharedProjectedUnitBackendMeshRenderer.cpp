#include "game/runtime/SharedProjectedUnitBackendMeshRenderer.h"
#include "game/runtime/SharedProjectedUnitBackendMeshPrep.h"
#include "game/runtime/SharedProjectedUnitBackendMeshTransforms.h"

#include "game/runtime/BackendMaterialShading.h"
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

    using WorldIndexedBatch = game::runtime::shared_world_batches::WorldIndexedBatch;
    using SharedTailFireAnchor = game::runtime::shared_tail_fire_fallback::Anchor;
    using DepthTri = game::runtime::shared_projected_scene::DepthTri;
    using DepthWorldTri = game::runtime::shared_projected_scene::DepthWorldTri;

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
        const auto safeNormalize = [](const glm::vec3& v) {
            const float lenSq = glm::dot(v, v);
            if (lenSq > 1e-12f) return glm::normalize(v);
            return glm::vec3(0.0f, 1.0f, 0.0f);
        };

        shared_projected_unit_backend_mesh_transforms::Resolver transforms;
        transforms.initialize(args, prep);

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

        const auto pushModelTriangle = [&](const glm::vec3& a,
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
                                            bool doubleSided) {
            float x1 = 0.0f;
            float y1 = 0.0f;
            float z1 = 0.0f;
            float x2 = 0.0f;
            float y2 = 0.0f;
            float z2 = 0.0f;
            float x3 = 0.0f;
            float y3 = 0.0f;
            float z3 = 0.0f;
            if (!supportsWorldTriangles3D) {
                if (!projectedDebug.projectWorld(a, x1, y1, z1) ||
                    !projectedDebug.projectWorld(b, x2, y2, z2) ||
                    !projectedDebug.projectWorld(c, x3, y3, z3)) {
                    return;
                }
                if ((z1 < 0.0f || z1 > 1.0f) &&
                    (z2 < 0.0f || z2 > 1.0f) &&
                    (z3 < 0.0f || z3 > 1.0f)) {
                    return;
                }
            }

            const float outAlpha = alpha;
            if (supportsWorldTriangles3D &&
                useIndexedWorldModelPath &&
                backendModelFastTexturedPathEnabled()) {
                std::size_t fastBatchIndex = static_cast<std::size_t>(submeshIndex);
                if (fastBatchIndex >= modelIndexedBatchesPerSubmesh.size()) fastBatchIndex = 0u;
                auto& fastBatch = modelIndexedBatchesPerSubmesh[fastBatchIndex];
                const bool fastTexturedBatch =
                    fastBatch.textureRgba != nullptr &&
                    fastBatch.textureWidth > 0 &&
                    fastBatch.textureHeight > 0;
                if (fastTexturedBatch) {
                    const glm::vec3 flatTint(1.0f, 1.0f, 1.0f);
                    const bool canReuseIndexedVertices =
                        fullIndexedMeshPath &&
                        fastBatchIndex < modelIndexedVertexRemap.size();
                    const auto appendFastVertex =
                        [&](std::uint32_t src,
                            const glm::vec3& pos,
                            const glm::vec2& uv) -> std::uint32_t {
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
                            const std::uint32_t next =
                                static_cast<std::uint32_t>(fastBatch.vertices.size());
                            fastBatch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                                pos.x,
                                pos.y,
                                pos.z,
                                uv.x,
                                uv.y,
                                flatTint.r,
                                flatTint.g,
                                flatTint.b,
                                outAlpha});
                            mapped = static_cast<int>(next);
                            return next;
                        }
                        if (fastBatch.vertices.size() >=
                            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                            return std::numeric_limits<std::uint32_t>::max();
                        }
                        const std::uint32_t next =
                            static_cast<std::uint32_t>(fastBatch.vertices.size());
                        fastBatch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                            pos.x,
                            pos.y,
                            pos.z,
                            uv.x,
                            uv.y,
                            flatTint.r,
                            flatTint.g,
                            flatTint.b,
                            outAlpha});
                        return next;
                    };

                    const std::uint32_t outI0 = appendFastVertex(src0, a, uv0);
                    const std::uint32_t outI1 = appendFastVertex(src1, b, uv1);
                    const std::uint32_t outI2 = appendFastVertex(src2, c, uv2);
                    if (outI0 == std::numeric_limits<std::uint32_t>::max() ||
                        outI1 == std::numeric_limits<std::uint32_t>::max() ||
                        outI2 == std::numeric_limits<std::uint32_t>::max()) {
                        return;
                    }
                    fastBatch.indices.push_back(outI0);
                    fastBatch.indices.push_back(outI1);
                    fastBatch.indices.push_back(outI2);
                    return;
                }
            }

            const glm::vec3 triCenter = (a + b + c) * (1.0f / 3.0f);
            const glm::vec3 rawFaceNormal = glm::cross(b - a, c - a);
            const float rawFaceLenSq = glm::dot(rawFaceNormal, rawFaceNormal);
            const glm::vec3 faceNormal = (rawFaceLenSq > 0.000001f)
                ? glm::normalize(rawFaceNormal)
                : safeNormalize(n0 + n1 + n2);
            glm::vec3 toCameraCenter = cameraWorldPos - triCenter;
            const float toCameraCenterLenSq = glm::dot(toCameraCenter, toCameraCenter);
            if (toCameraCenterLenSq > 0.000001f) {
                toCameraCenter = glm::normalize(toCameraCenter);
            } else {
                toCameraCenter = glm::vec3(0.0f, 0.0f, -1.0f);
            }
            const float faceFacing = std::clamp(glm::dot(faceNormal, toCameraCenter), -1.0f, 1.0f);
            if (backendModelBackfaceCullingEnabled() && !doubleSided && faceFacing <= 0.01f) {
                return;
            }
            const bool flipForBackface = doubleSided && (faceFacing < 0.0f);

            if (supportsWorldTriangles3D && useIndexedWorldModelPath) {
                std::size_t batchIndex = static_cast<std::size_t>(submeshIndex);
                if (batchIndex >= modelIndexedBatchesPerSubmesh.size()) batchIndex = 0u;
                auto& batch = modelIndexedBatchesPerSubmesh[batchIndex];
                const bool texturedBatch =
                    batch.textureRgba != nullptr &&
                    batch.textureWidth > 0 &&
                    batch.textureHeight > 0;

                if (texturedBatch) {
                    const auto shadeTint = [&](const glm::vec3& normal,
                                               const glm::vec3& worldPos) {
                        return runtime::backend_material::shadeVertexLitColor(
                            glm::vec3(1.0f),
                            normal,
                            lightDir,
                            cameraWorldPos - worldPos,
                            flipForBackface);
                    };
                    const glm::vec3 outC0 = shadeTint(n0, a);
                    const glm::vec3 outC1 = shadeTint(n1, b);
                    const glm::vec3 outC2 = shadeTint(n2, c);

                    const bool canReuseIndexedVertices =
                        fullIndexedMeshPath &&
                        batchIndex < modelIndexedVertexRemap.size();
                    const auto appendIndexedVertex =
                        [&](std::uint32_t src,
                            const glm::vec3& pos,
                            const glm::vec2& uv,
                            const glm::vec3& outColor) -> std::uint32_t {
                        if (canReuseIndexedVertices &&
                            src < modelIndexedVertexRemap[batchIndex].size()) {
                            int& mapped = modelIndexedVertexRemap[batchIndex][src];
                            if (mapped >= 0) {
                                return static_cast<std::uint32_t>(mapped);
                            }
                            if (batch.vertices.size() >=
                                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                                return std::numeric_limits<std::uint32_t>::max();
                            }
                            const std::uint32_t next =
                                static_cast<std::uint32_t>(batch.vertices.size());
                            batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                                pos.x,
                                pos.y,
                                pos.z,
                                uv.x,
                                uv.y,
                                outColor.r,
                                outColor.g,
                                outColor.b,
                                outAlpha});
                            mapped = static_cast<int>(next);
                            return next;
                        }
                        if (batch.vertices.size() >=
                            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                            return std::numeric_limits<std::uint32_t>::max();
                        }
                        const std::uint32_t next = static_cast<std::uint32_t>(batch.vertices.size());
                        batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                            pos.x,
                            pos.y,
                            pos.z,
                            uv.x,
                            uv.y,
                            outColor.r,
                            outColor.g,
                            outColor.b,
                            outAlpha});
                        return next;
                    };

                    const std::uint32_t outI0 = appendIndexedVertex(src0, a, uv0, outC0);
                    const std::uint32_t outI1 = appendIndexedVertex(src1, b, uv1, outC1);
                    const std::uint32_t outI2 = appendIndexedVertex(src2, c, uv2, outC2);
                    if (outI0 == std::numeric_limits<std::uint32_t>::max() ||
                        outI1 == std::numeric_limits<std::uint32_t>::max() ||
                        outI2 == std::numeric_limits<std::uint32_t>::max()) {
                        return;
                    }
                    batch.indices.push_back(outI0);
                    batch.indices.push_back(outI1);
                    batch.indices.push_back(outI2);
                    return;
                }

                const auto shadeColor = [&](const glm::vec3& baseColor,
                                            const glm::vec3& normal,
                                            const glm::vec3& worldPos) {
                    return runtime::backend_material::shadeVertexLitColor(
                        baseColor,
                        normal,
                        lightDir,
                        cameraWorldPos - worldPos,
                        flipForBackface);
                };
                const glm::vec3 shaded0 = shadeColor(baseColor0, n0, a);
                const glm::vec3 shaded1 = shadeColor(baseColor1, n1, b);
                const glm::vec3 shaded2 = shadeColor(baseColor2, n2, c);
                const std::size_t nextVertexCount = batch.vertices.size() + 3u;
                if (nextVertexCount >=
                    static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                    return;
                }

                const std::uint32_t base = static_cast<std::uint32_t>(batch.vertices.size());
                batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                    a.x, a.y, a.z, uv0.x, uv0.y, shaded0.r, shaded0.g, shaded0.b, outAlpha});
                batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                    b.x, b.y, b.z, uv1.x, uv1.y, shaded1.r, shaded1.g, shaded1.b, outAlpha});
                batch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                    c.x, c.y, c.z, uv2.x, uv2.y, shaded2.r, shaded2.g, shaded2.b, outAlpha});
                batch.indices.push_back(base + 0u);
                batch.indices.push_back(base + 1u);
                batch.indices.push_back(base + 2u);
                return;
            }

            const auto shadeColor = [&](const glm::vec3& baseColor,
                                        const glm::vec3& normal,
                                        const glm::vec3& worldPos) {
                return runtime::backend_material::shadeVertexLitColor(
                    baseColor,
                    normal,
                    lightDir,
                    cameraWorldPos - worldPos,
                    flipForBackface);
            };
            const glm::vec3 shaded0 = shadeColor(baseColor0, n0, a);
            const glm::vec3 shaded1 = shadeColor(baseColor1, n1, b);
            const glm::vec3 shaded2 = shadeColor(baseColor2, n2, c);
            const glm::vec3 shadedAvg = (shaded0 + shaded1 + shaded2) * (1.0f / 3.0f);

            if (supportsWorldTriangles3D) {
                IRenderBackend::WorldTriangle tri3d;
                tri3d.x1 = a.x;
                tri3d.y1 = a.y;
                tri3d.z1 = a.z;
                tri3d.x2 = b.x;
                tri3d.y2 = b.y;
                tri3d.z2 = b.z;
                tri3d.x3 = c.x;
                tri3d.y3 = c.y;
                tri3d.z3 = c.z;
                tri3d.r = shadedAvg.r;
                tri3d.g = shadedAvg.g;
                tri3d.b = shadedAvg.b;
                tri3d.a = outAlpha;
                tri3d.r1 = shaded0.r;
                tri3d.g1 = shaded0.g;
                tri3d.b1 = shaded0.b;
                tri3d.a1 = outAlpha;
                tri3d.r2 = shaded1.r;
                tri3d.g2 = shaded1.g;
                tri3d.b2 = shaded1.b;
                tri3d.a2 = outAlpha;
                tri3d.r3 = shaded2.r;
                tri3d.g3 = shaded2.g;
                tri3d.b3 = shaded2.b;
                tri3d.a3 = outAlpha;
                world3DTriangles.push_back(tri3d);
                return;
            }

            DepthTri dt;
            dt.tri.x1 = x1;
            dt.tri.y1 = y1;
            dt.tri.x2 = x2;
            dt.tri.y2 = y2;
            dt.tri.x3 = x3;
            dt.tri.y3 = y3;
            dt.tri.r = shadedAvg.r;
            dt.tri.g = shadedAvg.g;
            dt.tri.b = shadedAvg.b;
            dt.tri.a = outAlpha;
            dt.depth = (z1 + z2 + z3) * (1.0f / 3.0f);
            modelDepthTris.push_back(dt);
        };
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
                        const glm::vec3 pos = transforms.resolveWorldVertexPos(triNodeIndex, src, srcVertex);
                        const std::uint32_t next =
                            static_cast<std::uint32_t>(fastBatch.vertices.size());
                        fastBatch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                            pos.x,
                            pos.y,
                            pos.z,
                            srcVertex.uv.x,
                            srcVertex.uv.y,
                            fastTexturedTint.r,
                            fastTexturedTint.g,
                            fastTexturedTint.b,
                            fastTexturedAlpha});
                        mapped = static_cast<int>(next);
                        return next;
                    }
                    if (fastBatch.vertices.size() >=
                        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                        return std::numeric_limits<std::uint32_t>::max();
                    }
                    const glm::vec3 pos = transforms.resolveWorldVertexPos(triNodeIndex, src, srcVertex);
                    const std::uint32_t next =
                        static_cast<std::uint32_t>(fastBatch.vertices.size());
                    fastBatch.vertices.push_back(IRenderBackend::WorldMeshVertex{
                        pos.x,
                        pos.y,
                        pos.z,
                        srcVertex.uv.x,
                        srcVertex.uv.y,
                        fastTexturedTint.r,
                        fastTexturedTint.g,
                        fastTexturedTint.b,
                        fastTexturedAlpha});
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

            const auto sk0 = transforms.resolveWorldVertex(triNodeIndex, i0, v0);
            const auto sk1 = transforms.resolveWorldVertex(triNodeIndex, i1, v1);
            const auto sk2 = transforms.resolveWorldVertex(triNodeIndex, i2, v2);

            const glm::vec3& a = sk0.pos;
            const glm::vec3& b = sk1.pos;
            const glm::vec3& c = sk2.pos;
            const glm::vec3& n0 = sk0.normal;
            const glm::vec3& n1 = sk1.normal;
            const glm::vec3& n2 = sk2.normal;

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
            pushModelTriangle(
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

