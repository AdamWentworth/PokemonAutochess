#include "game/runtime/shared/projected/backend_mesh/SharedProjectedUnitBackendMeshCpuRewrite.h"

namespace mesh_persistent = game::runtime::shared_projected_unit_backend_mesh_persistent;
namespace persistent = game::runtime::shared_projected_render_items;

namespace game::runtime::shared_projected_unit_backend_mesh_cpu_rewrite {

void buildOrReuseCpuRewriteVertices(const Args& args) {
    if (!args.mesh || !args.srcBatch || !args.transforms || !args.dstBatch) {
        return;
    }

    const auto& srcBatch = *args.srcBatch;
    auto& dstBatch = *args.dstBatch;

    dstBatch.geometryCacheKey.clear();
    dstBatch.sharedVertices = nullptr;
    dstBatch.sharedVertexCount = 0u;
    dstBatch.indices.clear();
    if (!srcBatch.indices.empty()) {
        dstBatch.sharedIndices = srcBatch.indices.data();
        dstBatch.sharedIndexCount = srcBatch.indices.size();
    } else {
        dstBatch.sharedIndices = nullptr;
        dstBatch.sharedIndexCount = 0u;
    }

    persistent::ProjectedRenderItemEntry* cachedItem = args.canCacheCpuRewrite
        ? mesh_persistent::ensurePersistentRenderItem(args.persistentSync, args.itemIndex)
        : nullptr;

    const bool reuseCpuRewriteVertices =
        cachedItem &&
        cachedItem->cpuRewriteGeometryTemplateIdentity ==
            static_cast<const void*>(&srcBatch) &&
        cachedItem->cpuRewritePoseHash == args.poseHash &&
        cachedItem->cpuRewriteNeedsLitNormals ==
            static_cast<std::uint8_t>(args.needsLitNormals ? 1u : 0u) &&
        cachedItem->cpuRewriteNeedsTangents ==
            static_cast<std::uint8_t>(args.needsTangents ? 1u : 0u) &&
        cachedItem->cpuRewriteVertices.size() == srcBatch.sourceVertexIndices.size();

    std::vector<IRenderBackend::WorldMeshVertex>* rewrittenVertices = nullptr;
    if (reuseCpuRewriteVertices) {
        dstBatch.vertices.clear();
        dstBatch.sharedVertices = cachedItem->cpuRewriteVertices.data();
        dstBatch.sharedVertexCount = cachedItem->cpuRewriteVertices.size();
    } else if (cachedItem) {
        cachedItem->cpuRewriteVertices.resize(srcBatch.sourceVertexIndices.size());
        rewrittenVertices = &cachedItem->cpuRewriteVertices;
    } else {
        dstBatch.vertices.resize(srcBatch.sourceVertexIndices.size());
        rewrittenVertices = &dstBatch.vertices;
    }

    if (rewrittenVertices) {
        for (std::size_t vi = 0; vi < srcBatch.sourceVertexIndices.size(); ++vi) {
            const std::uint32_t srcIndex = srcBatch.sourceVertexIndices[vi];
            if (srcIndex >= args.mesh->vertices.size()) {
                continue;
            }

            const auto& srcVertex = args.mesh->vertices[srcIndex];
            IRenderBackend::WorldMeshVertex outVertex = srcBatch.gpuTemplateVertices[vi];
            const auto surface = args.transforms->resolveModelVertexSurface(
                args.resolvedTriNodeIndex,
                srcIndex,
                srcVertex,
                args.needsLitNormals,
                args.needsTangents);
            outVertex.x = surface.pos.x;
            outVertex.y = surface.pos.y;
            outVertex.z = surface.pos.z;
            if (args.needsLitNormals) {
                outVertex.nx = surface.normal.x;
                outVertex.ny = surface.normal.y;
                outVertex.nz = surface.normal.z;
            }
            if (args.needsTangents) {
                outVertex.tx = surface.tangent.x;
                outVertex.ty = surface.tangent.y;
                outVertex.tz = surface.tangent.z;
                outVertex.tw = surface.tangent.w;
            }
            (*rewrittenVertices)[vi] = outVertex;
        }
    }

    if (cachedItem) {
        cachedItem->cpuRewriteGeometryTemplateIdentity = static_cast<const void*>(&srcBatch);
        cachedItem->cpuRewritePoseHash = args.poseHash;
        cachedItem->cpuRewriteNeedsLitNormals =
            static_cast<std::uint8_t>(args.needsLitNormals ? 1u : 0u);
        cachedItem->cpuRewriteNeedsTangents =
            static_cast<std::uint8_t>(args.needsTangents ? 1u : 0u);
        dstBatch.vertices.clear();
        dstBatch.sharedVertices = cachedItem->cpuRewriteVertices.data();
        dstBatch.sharedVertexCount = cachedItem->cpuRewriteVertices.size();
    }
}

} // namespace game::runtime::shared_projected_unit_backend_mesh_cpu_rewrite

