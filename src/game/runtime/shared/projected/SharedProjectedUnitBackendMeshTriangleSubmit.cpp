#include "game/runtime/shared/projected/SharedProjectedUnitBackendMeshTriangleSubmit.h"

#include "game/runtime/render_prep/BackendMaterialShading.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

glm::vec3 safeNormalizeVec3(const glm::vec3& v) {
    const float lenSq = glm::dot(v, v);
    if (lenSq > 1e-12f) return glm::normalize(v);
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

} // namespace

namespace game::runtime::shared_projected_unit_backend_mesh_submit {

void TriangleSubmitter::initialize(const Args& args) {
    args_ = args;
}

void TriangleSubmitter::pushTriangle(const glm::vec3& a,
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
                                     const glm::vec4& t0,
                                     const glm::vec4& t1,
                                     const glm::vec4& t2,
                                     const glm::vec3& baseColor0,
                                     const glm::vec3& baseColor1,
                                     const glm::vec3& baseColor2,
                                     std::uint16_t submeshIndex,
                                     float alpha,
                                     bool doubleSided) {
    if (!args_.projectedDebug || !args_.modelIndexedBatchesPerSubmesh || !args_.modelIndexedVertexRemap ||
        !args_.modelDepthTris || !args_.world3DTriangles) {
        return;
    }

    auto& projectedDebug = *args_.projectedDebug;
    auto& modelIndexedBatchesPerSubmesh = *args_.modelIndexedBatchesPerSubmesh;
    auto& modelIndexedVertexRemap = *args_.modelIndexedVertexRemap;
    auto& modelDepthTris = *args_.modelDepthTris;
    auto& world3DTriangles = *args_.world3DTriangles;
    const auto prepareMutableIndexedBatch =
        [](shared_world_batches::WorldIndexedBatch& batch) {
            // Triangle-submit path rebuilds per-unit/per-pose vertex data, so any
            // previously assigned shared-geometry cache key is no longer valid.
            batch.geometryCacheKey.clear();
            batch.sharedVertices = nullptr;
            batch.sharedVertexCount = 0u;
            batch.sharedIndices = nullptr;
            batch.sharedIndexCount = 0u;
        };

    float x1 = 0.0f;
    float y1 = 0.0f;
    float z1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float z2 = 0.0f;
    float x3 = 0.0f;
    float y3 = 0.0f;
    float z3 = 0.0f;
    if (!args_.supportsWorldTriangles3D) {
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
    if (args_.supportsWorldTriangles3D &&
        args_.useIndexedWorldModelPath &&
        args_.fastTexturedPathEnabled) {
        std::size_t fastBatchIndex = static_cast<std::size_t>(submeshIndex);
        if (fastBatchIndex >= modelIndexedBatchesPerSubmesh.size()) fastBatchIndex = 0u;
        auto& fastBatch = modelIndexedBatchesPerSubmesh[fastBatchIndex];
        const bool fastTexturedBatch =
            fastBatch.textureRgba != nullptr &&
            fastBatch.textureWidth > 0 &&
            fastBatch.textureHeight > 0;
        if (fastTexturedBatch) {
            prepareMutableIndexedBatch(fastBatch);
            const glm::vec3 flatTint(1.0f, 1.0f, 1.0f);
            const bool canReuseIndexedVertices =
                args_.fullIndexedMeshPath &&
                fastBatchIndex < modelIndexedVertexRemap.size();
            const auto appendFastVertex =
                [&](std::uint32_t src,
                    const glm::vec3& pos,
                    const glm::vec2& uv,
                    const glm::vec3& normal,
                    const glm::vec4& tangent) -> std::uint32_t {
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
                    IRenderBackend::WorldMeshVertex outVertex{};
                    outVertex.x = pos.x;
                    outVertex.y = pos.y;
                    outVertex.z = pos.z;
                    outVertex.u = uv.x;
                    outVertex.v = uv.y;
                    outVertex.r = flatTint.r;
                    outVertex.g = flatTint.g;
                    outVertex.b = flatTint.b;
                    outVertex.a = outAlpha;
                    outVertex.nx = normal.x;
                    outVertex.ny = normal.y;
                    outVertex.nz = normal.z;
                    outVertex.tx = tangent.x;
                    outVertex.ty = tangent.y;
                    outVertex.tz = tangent.z;
                    outVertex.tw = tangent.w;
                    fastBatch.vertices.push_back(outVertex);
                    mapped = static_cast<int>(next);
                    return next;
                }
                if (fastBatch.vertices.size() >=
                    static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                    return std::numeric_limits<std::uint32_t>::max();
                }
                const std::uint32_t next =
                    static_cast<std::uint32_t>(fastBatch.vertices.size());
                IRenderBackend::WorldMeshVertex outVertex{};
                outVertex.x = pos.x;
                outVertex.y = pos.y;
                outVertex.z = pos.z;
                outVertex.u = uv.x;
                outVertex.v = uv.y;
                outVertex.r = flatTint.r;
                outVertex.g = flatTint.g;
                outVertex.b = flatTint.b;
                outVertex.a = outAlpha;
                outVertex.nx = normal.x;
                outVertex.ny = normal.y;
                outVertex.nz = normal.z;
                outVertex.tx = tangent.x;
                outVertex.ty = tangent.y;
                outVertex.tz = tangent.z;
                outVertex.tw = tangent.w;
                fastBatch.vertices.push_back(outVertex);
                return next;
            };

            const std::uint32_t outI0 = appendFastVertex(src0, a, uv0, n0, t0);
            const std::uint32_t outI1 = appendFastVertex(src1, b, uv1, n1, t1);
            const std::uint32_t outI2 = appendFastVertex(src2, c, uv2, n2, t2);
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
        : safeNormalizeVec3(n0 + n1 + n2);
    glm::vec3 toCameraCenter = args_.cameraWorldPos - triCenter;
    const float toCameraCenterLenSq = glm::dot(toCameraCenter, toCameraCenter);
    if (toCameraCenterLenSq > 0.000001f) {
        toCameraCenter = glm::normalize(toCameraCenter);
    } else {
        toCameraCenter = glm::vec3(0.0f, 0.0f, -1.0f);
    }
    const float faceFacing = std::clamp(glm::dot(faceNormal, toCameraCenter), -1.0f, 1.0f);
    if (args_.backfaceCullingEnabled && !doubleSided && faceFacing <= 0.01f) {
        return;
    }
    const bool flipForBackface = doubleSided && (faceFacing < 0.0f);

    if (args_.supportsWorldTriangles3D && args_.useIndexedWorldModelPath) {
        std::size_t batchIndex = static_cast<std::size_t>(submeshIndex);
        if (batchIndex >= modelIndexedBatchesPerSubmesh.size()) batchIndex = 0u;
        auto& batch = modelIndexedBatchesPerSubmesh[batchIndex];
        const bool texturedBatch =
            batch.textureRgba != nullptr &&
            batch.textureWidth > 0 &&
            batch.textureHeight > 0;

        if (texturedBatch) {
            prepareMutableIndexedBatch(batch);
            const glm::vec3 outC0 = baseColor0;
            const glm::vec3 outC1 = baseColor1;
            const glm::vec3 outC2 = baseColor2;

            const bool canReuseIndexedVertices =
                args_.fullIndexedMeshPath &&
                batchIndex < modelIndexedVertexRemap.size();
            const auto appendIndexedVertex =
                [&](std::uint32_t src,
                    const glm::vec3& pos,
                    const glm::vec2& uv,
                    const glm::vec3& outColor,
                    const glm::vec3& normal,
                    const glm::vec4& tangent) -> std::uint32_t {
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
                    IRenderBackend::WorldMeshVertex outVertex{};
                    outVertex.x = pos.x;
                    outVertex.y = pos.y;
                    outVertex.z = pos.z;
                    outVertex.u = uv.x;
                    outVertex.v = uv.y;
                    outVertex.r = outColor.r;
                    outVertex.g = outColor.g;
                    outVertex.b = outColor.b;
                    outVertex.a = outAlpha;
                    outVertex.nx = normal.x;
                    outVertex.ny = normal.y;
                    outVertex.nz = normal.z;
                    outVertex.tx = tangent.x;
                    outVertex.ty = tangent.y;
                    outVertex.tz = tangent.z;
                    outVertex.tw = tangent.w;
                    batch.vertices.push_back(outVertex);
                    mapped = static_cast<int>(next);
                    return next;
                }
                if (batch.vertices.size() >=
                    static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                    return std::numeric_limits<std::uint32_t>::max();
                }
                const std::uint32_t next = static_cast<std::uint32_t>(batch.vertices.size());
                IRenderBackend::WorldMeshVertex outVertex{};
                outVertex.x = pos.x;
                outVertex.y = pos.y;
                outVertex.z = pos.z;
                outVertex.u = uv.x;
                outVertex.v = uv.y;
                outVertex.r = outColor.r;
                outVertex.g = outColor.g;
                outVertex.b = outColor.b;
                outVertex.a = outAlpha;
                outVertex.nx = normal.x;
                outVertex.ny = normal.y;
                outVertex.nz = normal.z;
                outVertex.tx = tangent.x;
                outVertex.ty = tangent.y;
                outVertex.tz = tangent.z;
                outVertex.tw = tangent.w;
                batch.vertices.push_back(outVertex);
                return next;
            };

            const std::uint32_t outI0 = appendIndexedVertex(src0, a, uv0, outC0, n0, t0);
            const std::uint32_t outI1 = appendIndexedVertex(src1, b, uv1, outC1, n1, t1);
            const std::uint32_t outI2 = appendIndexedVertex(src2, c, uv2, outC2, n2, t2);
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

        const glm::vec3 shaded0 = baseColor0;
        const glm::vec3 shaded1 = baseColor1;
        const glm::vec3 shaded2 = baseColor2;
        prepareMutableIndexedBatch(batch);
        const std::size_t nextVertexCount = batch.vertices.size() + 3u;
        if (nextVertexCount >=
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            return;
        }

        const std::uint32_t base = static_cast<std::uint32_t>(batch.vertices.size());
        IRenderBackend::WorldMeshVertex outV0{};
        outV0.x = a.x; outV0.y = a.y; outV0.z = a.z;
        outV0.u = uv0.x; outV0.v = uv0.y;
        outV0.r = shaded0.r; outV0.g = shaded0.g; outV0.b = shaded0.b; outV0.a = outAlpha;
        outV0.nx = n0.x; outV0.ny = n0.y; outV0.nz = n0.z;
        outV0.tx = t0.x; outV0.ty = t0.y; outV0.tz = t0.z; outV0.tw = t0.w;
        batch.vertices.push_back(outV0);
        IRenderBackend::WorldMeshVertex outV1{};
        outV1.x = b.x; outV1.y = b.y; outV1.z = b.z;
        outV1.u = uv1.x; outV1.v = uv1.y;
        outV1.r = shaded1.r; outV1.g = shaded1.g; outV1.b = shaded1.b; outV1.a = outAlpha;
        outV1.nx = n1.x; outV1.ny = n1.y; outV1.nz = n1.z;
        outV1.tx = t1.x; outV1.ty = t1.y; outV1.tz = t1.z; outV1.tw = t1.w;
        batch.vertices.push_back(outV1);
        IRenderBackend::WorldMeshVertex outV2{};
        outV2.x = c.x; outV2.y = c.y; outV2.z = c.z;
        outV2.u = uv2.x; outV2.v = uv2.y;
        outV2.r = shaded2.r; outV2.g = shaded2.g; outV2.b = shaded2.b; outV2.a = outAlpha;
        outV2.nx = n2.x; outV2.ny = n2.y; outV2.nz = n2.z;
        outV2.tx = t2.x; outV2.ty = t2.y; outV2.tz = t2.z; outV2.tw = t2.w;
        batch.vertices.push_back(outV2);
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
            args_.lightDir,
            args_.cameraWorldPos - worldPos,
            flipForBackface);
    };
    const glm::vec3 shaded0 = shadeColor(baseColor0, n0, a);
    const glm::vec3 shaded1 = shadeColor(baseColor1, n1, b);
    const glm::vec3 shaded2 = shadeColor(baseColor2, n2, c);
    const glm::vec3 shadedAvg = (shaded0 + shaded1 + shaded2) * (1.0f / 3.0f);

    if (args_.supportsWorldTriangles3D) {
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

    shared_projected_scene::DepthTri dt;
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
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_submit

